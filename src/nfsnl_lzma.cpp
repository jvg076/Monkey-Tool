// nfsnl_lzma.cpp - LZMA (LZMA1, the 7-Zip / "LZMA alone" codec)
//
// NFS No Limits' China build (Tencent, com.tencent.tmgp.NFSNL) packs most of
// its FMOBB files with LZMA where the other builds use LZHAM or Brotli: a
// properties byte (lc / lp / pb), the dictionary size (4 bytes), then the
// range-coded stream, with no size field - the FMOBB index states it.
//
// A plain decoder after the format's reference description: the whole output
// is in memory, so it serves as the dictionary too.
#include "nfsnl.h"

#include <cstring>

namespace nfsnl {

namespace {

const int kNumStates = 12;
const unsigned kProbInit = 1024;   // half of 2^11

struct Range {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t range = 0xFFFFFFFFu, code = 0;
    bool bad = false;
    uint8_t next() {
        if (p >= end) { bad = true; return 0; }
        return *p++;
    }
    bool init() {
        if (next() != 0) return false;           // the stream always starts with 0
        for (int i = 0; i < 4; ++i) code = (code << 8) | next();
        return !bad && code != range;
    }
    void normalize() {
        if (range < (1u << 24)) { range <<= 8; code = (code << 8) | next(); }
    }
    unsigned bit(uint16_t& prob) {
        uint32_t bound = (range >> 11) * prob;
        unsigned b;
        if (code < bound) {
            prob = (uint16_t)(prob + ((2048 - prob) >> 5));
            range = bound;
            b = 0;
        } else {
            prob = (uint16_t)(prob - (prob >> 5));
            code -= bound;
            range -= bound;
            b = 1;
        }
        normalize();
        return b;
    }
    uint32_t direct(unsigned n) {
        uint32_t res = 0;
        do {
            range >>= 1;
            code -= range;
            uint32_t t = 0u - (code >> 31);
            code += range & t;
            if (code == range) bad = true;
            normalize();
            res = (res << 1) + (t + 1);
        } while (--n);
        return res;
    }
};

unsigned tree(Range& rc, uint16_t* probs, unsigned bits) {
    unsigned m = 1;
    for (unsigned i = 0; i < bits; ++i) m = (m << 1) + rc.bit(probs[m]);
    return m - (1u << bits);
}

unsigned reverseTree(Range& rc, uint16_t* probs, unsigned bits) {
    unsigned m = 1, sym = 0;
    for (unsigned i = 0; i < bits; ++i) {
        unsigned b = rc.bit(probs[m]);
        m = (m << 1) + b;
        sym |= b << i;
    }
    return sym;
}

struct LenDecoder {
    uint16_t choice = kProbInit, choice2 = kProbInit;
    uint16_t low[16][1 << 3], mid[16][1 << 3], high[1 << 8];
    LenDecoder() {
        for (auto& a : low) for (auto& p : a) p = kProbInit;
        for (auto& a : mid) for (auto& p : a) p = kProbInit;
        for (auto& p : high) p = kProbInit;
    }
    unsigned decode(Range& rc, unsigned posState) {
        if (!rc.bit(choice)) return tree(rc, low[posState], 3);
        if (!rc.bit(choice2)) return 8 + tree(rc, mid[posState], 3);
        return 16 + tree(rc, high, 8);
    }
};

} // namespace

bool lzmaDecompress(const uint8_t* src, size_t n, size_t outSize, Bytes& out) {
    out.clear();
    if (n < 6 || outSize == 0 || outSize > (1u << 30)) return false;
    unsigned d = src[0];
    if (d >= 9 * 5 * 5) return false;
    unsigned lc = d % 9;
    d /= 9;
    unsigned lp = d % 5, pb = d / 5;
    if (lc > 8 || lp > 4 || pb > 4) return false;

    Range rc;
    rc.p = src + 5;
    rc.end = src + n;
    if (!rc.init()) return false;

    std::vector<uint16_t> literal((size_t)0x300 << (lc + lp), (uint16_t)kProbInit);
    uint16_t isMatch[kNumStates << 4], isRep[kNumStates], isRepG0[kNumStates], isRepG1[kNumStates],
             isRepG2[kNumStates], isRep0Long[kNumStates << 4];
    uint16_t posSlot[4][1 << 6], posDecoders[1 + 114], align[1 << 4];
    for (auto& p : isMatch) p = kProbInit;
    for (auto& p : isRep) p = kProbInit;
    for (auto& p : isRepG0) p = kProbInit;
    for (auto& p : isRepG1) p = kProbInit;
    for (auto& p : isRepG2) p = kProbInit;
    for (auto& p : isRep0Long) p = kProbInit;
    for (auto& a : posSlot) for (auto& p : a) p = kProbInit;
    for (auto& p : posDecoders) p = kProbInit;
    for (auto& p : align) p = kProbInit;
    LenDecoder lenDec, repLenDec;

    out.reserve(outSize);
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    const unsigned pbMask = (1u << pb) - 1, lpMask = (1u << lp) - 1;

    while (out.size() < outSize) {
        if (rc.bad) return false;
        size_t pos = out.size();
        unsigned posState = (unsigned)pos & pbMask;
        if (!rc.bit(isMatch[(state << 4) + posState])) {
            // a literal byte
            unsigned prev = pos ? out[pos - 1] : 0;
            unsigned litState = (((unsigned)pos & lpMask) << lc) + (prev >> (8 - lc));
            uint16_t* probs = &literal[(size_t)0x300 * litState];
            unsigned sym = 1;
            if (state >= 7 && pos > rep0) {
                unsigned matchByte = out[pos - rep0 - 1];
                do {
                    unsigned matchBit = (matchByte >> 7) & 1;
                    matchByte <<= 1;
                    unsigned b = rc.bit(probs[((1 + matchBit) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (matchBit != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.bit(probs[sym]);
            out.push_back((uint8_t)(sym - 0x100));
            state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
            continue;
        }
        unsigned len;
        if (rc.bit(isRep[state])) {
            if (pos == 0) return false;
            if (!rc.bit(isRepG0[state])) {
                if (!rc.bit(isRep0Long[(state << 4) + posState])) {
                    // a single byte from the last distance
                    state = state < 7 ? 9 : 11;
                    out.push_back(out[pos - rep0 - 1]);
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc.bit(isRepG1[state])) {
                    dist = rep1;
                } else {
                    if (!rc.bit(isRepG2[state])) {
                        dist = rep2;
                    } else {
                        dist = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = repLenDec.decode(rc, posState);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            len = lenDec.decode(rc, posState);
            state = state < 7 ? 7 : 10;
            // the distance
            unsigned lenState = len < 3 ? len : 3;
            unsigned slot = tree(rc, posSlot[lenState], 6);
            if (slot < 4) {
                rep0 = slot;
            } else {
                unsigned direct = (slot >> 1) - 1;
                uint32_t dist = (2u | (slot & 1)) << direct;
                if (slot < 14) {
                    dist += reverseTree(rc, posDecoders + dist - slot, direct);
                } else {
                    dist += rc.direct(direct - 4) << 4;
                    dist += reverseTree(rc, align, 4);
                }
                rep0 = dist;
            }
            if (rep0 == 0xFFFFFFFFu) break;          // the end marker
        }
        len += 2;
        if (rep0 >= out.size()) return false;
        size_t from = out.size() - rep0 - 1;
        for (unsigned k = 0; k < len && out.size() < outSize; ++k) out.push_back(out[from + k]);
    }
    return !rc.bad && out.size() == outSize;
}

} // namespace nfsnl
