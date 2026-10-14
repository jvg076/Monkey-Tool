// nfsnl_rr3career.cpp - Real Racing 3's career file, data/events.dat(.nct)
//
// "RRCE", version 0x82, read by the loader at 0xc74fb8 in 14.0.1's arm64
// library. Everything is little-endian: i32, f32, a byte (u8 / bool) and
// strings as u32 length + bytes. Lists are an i32 count and the elements.
// Read in this order, with nothing between:
//
//   "RRCE", i32 0x82
//   3 x 17 lists of f32       reward / fame scale per finishing position,
//                             indexed by event type
//   list<TrackInfo>           track display name, f32, bool
//   list<Series>              ("streams" in the game)
//   list<Tier>                each names its series; holds the allowed cars
//                             and the events
//   list<SuperGroup>          the career menu: Motorsports, Career, each a
//                             tree of group nodes whose pages list series ids
//   the AI name pools, AI grids, AI drivers, rolling starts, limited-time
//   series (which point at series by their INDEX in the list) and reward
//   schedules - kept here as one block of bytes
//
// Everything is kept exactly, unknown fields included, so the file writes
// back byte for byte; see docs/FORMATS.md for every field.
#include "nfsnl.h"
#include "nfsnl_save.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

namespace nfsnl {

namespace {

void saves_md5(const Bytes& b, uint8_t d[16]) { saves::md5(b.data(), b.size(), d); }

struct Rd {
    const uint8_t* p; size_t n, i = 0; bool bad = false; std::string why;
    Rd(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
    void fail(const char* w) { if (!bad) { bad = true; char b[96]; snprintf(b, sizeof(b), "%s at byte %zu", w, i); why = b; } i = n; }
    uint32_t u32() { if (i + 4 > n) { fail("ran past the end"); return 0; } uint32_t v = (uint32_t)p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24; i += 4; return v; }
    int32_t i32() { return (int32_t)u32(); }
    uint8_t u8() { if (i + 1 > n) { fail("ran past the end"); return 0; } return p[i++]; }
    std::string str() {
        uint32_t L = u32();
        if (bad) return std::string();
        if (L > 0x800 || L > n - i) { fail("a string longer than the game allows"); return std::string(); }
        std::string s((const char*)p + i, L); i += L; return s;
    }
    uint32_t count() { int32_t c = i32(); if (c < 0 || c > 10000000) { fail("an impossible count"); return 0; } return (uint32_t)c; }
};

struct Wr {
    Bytes b;
    void u32(uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back((uint8_t)(v >> (8 * k))); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void u8(uint8_t v) { b.push_back(v); }
    void str(const std::string& s) { u32((uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
};

// a record by a schema: i i32, f f32, b byte, s string
void readRec(Rd& r, const char* schema, Rr3Fields& out) {
    out.clear();
    for (const char* k = schema; *k && !r.bad; ++k) {
        Rr3Val v;
        v.t = *k;
        if (*k == 's') v.s = r.str();
        else if (*k == 'b') v.u = r.u8();
        else v.u = r.u32();
        out.push_back(v);
    }
}
void writeRec(Wr& w, const Rr3Fields& rec) {
    for (const Rr3Val& v : rec) {
        if (v.t == 's') w.str(v.s);
        else if (v.t == 'b') w.u8((uint8_t)v.u);
        else w.u32(v.u);
    }
}

const char kEventA[] = "iisiiiiiiisbisiififsifsbbi";  // .. up to unk_0xc0
const char kEventB[] = "iifibbibbiiiib";              // after the optional extra
const char kSeriesHead[] = "isis";
const char kSeriesTail[] = "ibbssib";
const char kTierHead[] = "ssiiiiib";
const char kNodeBase[] = "ssssss";
const char kNodeBase2[] = "ssssss";                      // locked message + 5

void readEvent(Rd& r, Rr3Event& e) {
    readRec(r, kEventA, e.a);
    e.extraPresent = r.u8();
    e.extra.clear();
    if (e.extraPresent) readRec(r, "bff", e.extra);
    readRec(r, kEventB, e.b);
}
void writeEvent(Wr& w, const Rr3Event& e) {
    writeRec(w, e.a);
    w.u8(e.extraPresent);
    if (e.extraPresent) writeRec(w, e.extra);
    writeRec(w, e.b);
}

void readNode(Rd& r, Rr3GroupNode& n, int depth) {
    if (depth > 32) { r.fail("group nodes nested too deep"); return; }
    n.type = r.i32();
    readRec(r, kNodeBase, n.base);
    for (uint32_t k = 0, c = r.count(); k < c && !r.bad; ++k) n.logos.push_back(r.str());
    for (uint32_t k = 0, c = r.count(); k < c && !r.bad; ++k) n.packs.push_back(r.str());
    readRec(r, kNodeBase2, n.base2);
    for (uint32_t k = 0, c = r.count(); k < c && !r.bad; ++k) {
        n.children.emplace_back();
        readNode(r, n.children.back(), depth + 1);
    }
    if (n.type == 0 || n.type == 1) {
        for (uint32_t k = 0, c = r.count(); k < c && !r.bad; ++k) {
            std::vector<int32_t> page;
            for (uint32_t q = 0, m = r.count(); q < m && !r.bad; ++q) page.push_back(r.i32());
            n.pages.push_back(page);
        }
    } else if (n.type == 2) {
        // series id, 3 lists of series ids, i32, 5 str, bool, i32, 14 str,
        // list<{i32, 5 str, i32, str}> - kept as bytes, nothing here edits it
        size_t from = r.i;
        r.i32();
        for (int l = 0; l < 3; ++l) for (uint32_t q = 0, m = r.count(); q < m && !r.bad; ++q) r.i32();
        r.i32();
        for (int k = 0; k < 5; ++k) r.str();
        r.u8();
        r.i32();
        for (int k = 0; k < 14; ++k) r.str();
        for (uint32_t q = 0, m = r.count(); q < m && !r.bad; ++q) {
            r.i32();
            for (int k = 0; k < 5; ++k) r.str();
            r.i32();
            r.str();
        }
        if (!r.bad) n.type2.assign(r.p + from, r.p + r.i);
    } else {
        r.fail("a group node of an unknown type");
    }
}
void writeNode(Wr& w, const Rr3GroupNode& n) {
    w.i32(n.type);
    writeRec(w, n.base);
    w.u32((uint32_t)n.logos.size());
    for (const std::string& s : n.logos) w.str(s);
    w.u32((uint32_t)n.packs.size());
    for (const std::string& s : n.packs) w.str(s);
    writeRec(w, n.base2);
    w.u32((uint32_t)n.children.size());
    for (const Rr3GroupNode& c : n.children) writeNode(w, c);
    if (n.type == 0 || n.type == 1) {
        w.u32((uint32_t)n.pages.size());
        for (const auto& page : n.pages) {
            w.u32((uint32_t)page.size());
            for (int32_t id : page) w.i32(id);
        }
    } else {
        w.b.insert(w.b.end(), n.type2.begin(), n.type2.end());
    }
}

} // namespace

// ---- accessors

int32_t Rr3Series::id() const { return head.empty() ? -1 : (int32_t)head[0].u; }
const std::string& Rr3Series::titleKey() const { return head[1].s; }
const std::string& Rr3Series::descKey() const { return head[3].s; }
int32_t Rr3Series::type() const { return (int32_t)head[2].u; }
const std::string& Rr3Series::completionReward() const { return tail[3].s; }

int32_t Rr3Tier::id() const { return (int32_t)head[3].u; }
int32_t Rr3Tier::seriesId() const { return (int32_t)head[4].u; }
int32_t Rr3Tier::indexInSeries() const { return (int32_t)head[2].u; }

int32_t Rr3Event::type() const { return (int32_t)a[0].u; }
int32_t Rr3Event::id() const { return (int32_t)a[1].u; }
const std::string& Rr3Event::track() const { return a[2].s; }
int32_t Rr3Event::laps() const { return (int32_t)a[3].u; }
int32_t Rr3Event::fame() const { return (int32_t)a[7].u; }
int32_t Rr3Event::reward() const { return (int32_t)a[8].u; }

// ---- reading and writing

bool rr3CareerRead(const uint8_t* plain, size_t len, Rr3Career& c, std::string& why) {
    c = Rr3Career();
    Rd r(plain, len);
    if (len < 8 || memcmp(plain, "RRCE", 4) != 0) { why = "not a career file (no RRCE)"; return false; }
    r.i = 4;
    c.version = r.u32();
    if (c.version != 0x82) {
        char b[96];
        snprintf(b, sizeof(b), "career file version 0x%X - the tool reads 0x82 (Real Racing 3 14.x)", c.version);
        why = b;
        return false;
    }
    size_t from = r.i;
    for (int t = 0; t < 3 * 17 && !r.bad; ++t)
        for (uint32_t k = 0, m = r.count(); k < m && !r.bad; ++k) r.u32();
    c.scales.assign(plain + from, plain + r.i);
    for (uint32_t k = 0, m = r.count(); k < m && !r.bad; ++k) {
        Rr3Fields t;
        readRec(r, "sfb", t);
        c.tracks.push_back(t);
    }
    for (uint32_t k = 0, m = r.count(); k < m && !r.bad; ++k) {
        Rr3Series s;
        readRec(r, kSeriesHead, s.head);
        for (int q = 0; q < 3 && !r.bad; ++q)
            for (uint32_t j = 0, mm = r.count(); j < mm && !r.bad; ++j) { uint32_t x = r.u32(); s.curves[q].push_back({ x, r.u32() }); }
        for (uint32_t g = 0, gm = r.count(); g < gm && !r.bad; ++g) {
            std::vector<Rr3Fields> group;
            for (uint32_t j = 0, jm = r.count(); j < jm && !r.bad; ++j) {
                Rr3Fields q;
                readRec(r, "siis", q);
                group.push_back(q);
            }
            s.requirements.push_back(group);
        }
        readRec(r, kSeriesTail, s.tail);
        c.series.push_back(s);
    }
    for (uint32_t k = 0, m = r.count(); k < m && !r.bad; ++k) {
        Rr3Tier t;
        readRec(r, kTierHead, t.head);
        for (uint32_t j = 0, jm = r.count(); j < jm && !r.bad; ++j) { int32_t id = r.i32(); t.cars.push_back({ id, r.u8() }); }
        readRec(r, "sss", t.banners);
        for (uint32_t j = 0, jm = r.count(); j < jm && !r.bad; ++j) {
            Rr3Event e;
            readEvent(r, e);
            t.events.push_back(e);
        }
        c.tiers.push_back(t);
    }
    for (uint32_t k = 0, m = r.count(); k < m && !r.bad; ++k) {
        Rr3SuperGroup g;
        g.kind = r.i32();
        g.name = r.str();
        g.card = r.str();
        for (uint32_t j = 0, jm = r.count(); j < jm && !r.bad; ++j) g.packs.push_back(r.str());
        for (uint32_t j = 0, jm = r.count(); j < jm && !r.bad; ++j) {
            g.groups.emplace_back();
            readNode(r, g.groups.back(), 0);
        }
        c.superGroups.push_back(g);
    }
    // the rest: counted on the way, kept as bytes
    size_t restFrom = r.i;
    c.namePools = r.count();
    for (uint32_t k = 0; k < c.namePools && !r.bad; ++k) {
        uint32_t n = r.count();          // the pool's count comes before its name
        r.str();
        for (uint32_t q = 0; q < n && !r.bad; ++q) r.str();
    }
    c.grids = r.count();
    for (uint32_t k = 0; k < c.grids && !r.bad; ++k) {
        c.gridNames.push_back(r.str());
        r.u8();
        for (uint32_t q = 0, m = r.count(); q < m && !r.bad; ++q) if (r.u8() == 0) r.str();
    }
    c.rest.assign(plain + restFrom, plain + len);
    if (r.bad) { why = "career file: " + r.why; return false; }
    c.ok = true;
    return true;
}

Bytes rr3CareerWrite(const Rr3Career& c) {
    Wr w;
    w.b.insert(w.b.end(), { 'R', 'R', 'C', 'E' });
    w.u32(c.version);
    w.b.insert(w.b.end(), c.scales.begin(), c.scales.end());
    w.u32((uint32_t)c.tracks.size());
    for (const Rr3Fields& t : c.tracks) writeRec(w, t);
    w.u32((uint32_t)c.series.size());
    for (const Rr3Series& s : c.series) {
        writeRec(w, s.head);
        for (int q = 0; q < 3; ++q) {
            w.u32((uint32_t)s.curves[q].size());
            for (const auto& xy : s.curves[q]) { w.u32(xy.first); w.u32(xy.second); }
        }
        w.u32((uint32_t)s.requirements.size());
        for (const auto& g : s.requirements) {
            w.u32((uint32_t)g.size());
            for (const Rr3Fields& q : g) writeRec(w, q);
        }
        writeRec(w, s.tail);
    }
    w.u32((uint32_t)c.tiers.size());
    for (const Rr3Tier& t : c.tiers) {
        writeRec(w, t.head);
        w.u32((uint32_t)t.cars.size());
        for (const auto& car : t.cars) { w.i32(car.first); w.u8(car.second); }
        writeRec(w, t.banners);
        w.u32((uint32_t)t.events.size());
        for (const Rr3Event& e : t.events) writeEvent(w, e);
    }
    w.u32((uint32_t)c.superGroups.size());
    for (const Rr3SuperGroup& g : c.superGroups) {
        w.i32(g.kind);
        w.str(g.name);
        w.str(g.card);
        w.u32((uint32_t)g.packs.size());
        for (const std::string& s : g.packs) w.str(s);
        w.u32((uint32_t)g.groups.size());
        for (const Rr3GroupNode& n : g.groups) writeNode(w, n);
    }
    w.b.insert(w.b.end(), c.rest.begin(), c.rest.end());
    return w.b;
}

// ---- looking things up

std::vector<std::string> rr3CareerTracks(const Rr3Career& c) {
    std::vector<std::string> v;
    for (const Rr3Fields& t : c.tracks) v.push_back(t[0].s);
    return v;
}

namespace {
void findNodes(std::vector<Rr3GroupNode>& nodes, const std::string& path, std::vector<std::pair<std::string, Rr3GroupNode*>>& out) {
    for (Rr3GroupNode& n : nodes) {
        std::string p = path + "/" + n.base[0].s;
        out.push_back({ p, &n });
        findNodes(n.children, p, out);
    }
}
bool nodeHas(const Rr3GroupNode& n, int32_t id) {
    for (const auto& page : n.pages) for (int32_t x : page) if (x == id) return true;
    for (const Rr3GroupNode& ch : n.children) if (nodeHas(ch, id)) return true;
    return false;
}
}

std::vector<Rr3CareerGroup> rr3CareerGroups(Rr3Career& c) {
    std::vector<Rr3CareerGroup> out;
    for (size_t g = 0; g < c.superGroups.size(); ++g) {
        std::vector<std::pair<std::string, Rr3GroupNode*>> nodes;
        findNodes(c.superGroups[g].groups, c.superGroups[g].name, nodes);
        for (auto& n : nodes) {
            Rr3CareerGroup G;
            G.path = n.first;
            G.key = n.second->base[0].s;
            G.card = n.second->base[3].s;
            G.type = n.second->type;
            G.node = n.second;
            for (const auto& page : n.second->pages) G.series += page.size();
            out.push_back(G);
        }
    }
    return out;
}

std::string rr3CareerGroupOf(const Rr3Career& c, int32_t seriesId) {
    for (const Rr3SuperGroup& g : c.superGroups)
        for (const Rr3GroupNode& n : g.groups) {
            std::vector<std::pair<std::string, const Rr3GroupNode*>> stack{ { g.name + "/" + n.base[0].s, &n } };
            while (!stack.empty()) {
                auto cur = stack.back();
                stack.pop_back();
                for (const auto& page : cur.second->pages) for (int32_t x : page) if (x == seriesId) return cur.first;
                for (const Rr3GroupNode& ch : cur.second->children) stack.push_back({ cur.first + "/" + ch.base[0].s, &ch });
            }
        }
    return std::string();
}

// ---- text: GAMETEXT_<key> lines in gametext.txt and every language file

Rr3Texts::Rr3Texts() {}

bool Rr3Texts::load(const std::vector<std::string>& assets, const std::function<bool(const std::string&, Bytes&)>& read) {
    files.clear();
    keyLine.clear();
    for (const std::string& a : assets) {
        std::string leaf = baseName(a), l = leaf;
        for (char& ch : l) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        bool gt = l == "gametext.txt";
        bool lang = l.compare(0, 5, "text_") == 0 && l.size() > 9 && l.compare(l.size() - 4, 4, ".txt") == 0;
        if (!gt && !lang) continue;
        Bytes raw;
        if (!read(a, raw) || raw.empty()) continue;
        File f;
        f.leaf = leaf;
        f.plainOnDisk = rr3TextIsPlain(raw.data(), raw.size());
        Bytes p;
        if (f.plainOnDisk) p = raw; else rr3TextTransform(raw.data(), raw.size(), p);
        std::string s(p.begin(), p.end());
        size_t i = 0;
        while (i <= s.size()) {
            size_t e = s.find("\r\n", i);
            if (e == std::string::npos) { f.lines.push_back(s.substr(i)); break; }
            f.lines.push_back(s.substr(i, e - i));
            i = e + 2;
        }
        if (gt) files.insert(files.begin(), f);
        else files.push_back(f);
    }
    if (files.empty() || files[0].leaf.size() != 12) { files.clear(); return false; }
    for (size_t k = 0; k < files[0].lines.size(); ++k) keyLine[files[0].lines[k]] = k;
    for (size_t k = 0; k < files.size(); ++k) {
        std::string l = files[k].leaf;
        for (char& ch : l) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (l == "text_en.txt") english = (int)k;
    }
    return true;
}

std::string Rr3Texts::get(const std::string& key) const {
    auto it = keyLine.find(key.compare(0, 9, "GAMETEXT_") == 0 ? key : "GAMETEXT_" + key);
    if (it == keyLine.end() || english < 0 || it->second >= files[english].lines.size()) return std::string();
    std::string v = files[english].lines[it->second];
    if (v.size() >= 3 && (uint8_t)v[0] == 0xEF && (uint8_t)v[1] == 0xBB && (uint8_t)v[2] == 0xBF) v = v.substr(3);
    return v;
}

void Rr3Texts::set(const std::string& key, const std::string& text) {
    if (files.empty()) return;
    std::string k = key.compare(0, 9, "GAMETEXT_") == 0 ? key : "GAMETEXT_" + key;
    auto it = keyLine.find(k);
    if (it == keyLine.end()) {
        // a new key: one more line in every file, at the same place, before
        // the closing empty line
        File& g = files[0];
        size_t at = (!g.lines.empty() && g.lines.back().empty()) ? g.lines.size() - 1 : g.lines.size();
        for (size_t q = 0; q < files.size(); ++q) {
            File& f = files[q];
            while (f.lines.size() < at) f.lines.push_back(std::string());
            f.lines.insert(f.lines.begin() + at, q == 0 ? k : text);
            f.changed = true;
        }
        keyLine[k] = at;
        return;
    }
    size_t line = it->second;
    for (size_t q = 1; q < files.size(); ++q) {
        File& f = files[q];
        while (f.lines.size() <= line) f.lines.push_back(std::string());
        bool bom = line == 0 && f.lines[0].size() >= 3 && (uint8_t)f.lines[0][0] == 0xEF;
        std::string v = bom ? std::string("\xEF\xBB\xBF") + text : text;
        if (f.lines[line] != v) { f.lines[line] = v; f.changed = true; }
    }
}

std::vector<std::pair<std::string, Bytes>> Rr3Texts::changedFiles() const {
    std::vector<std::pair<std::string, Bytes>> out;
    for (const File& f : files) {
        if (!f.changed) continue;
        std::string s;
        for (size_t k = 0; k < f.lines.size(); ++k) { if (k) s += "\r\n"; s += f.lines[k]; }
        Bytes p(s.begin(), s.end());
        if (f.plainOnDisk) out.push_back({ f.leaf, p });
        else { Bytes e; rr3TextTransform(p.data(), p.size(), e); out.push_back({ f.leaf, e }); }
    }
    return out;
}

// ---- making a series

namespace {
std::string upperSlug(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch >= 'a' && ch <= 'z') o += (char)(ch - 'a' + 'A');
        else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) o += ch;
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o.empty() ? std::string("CUSTOM") : o;
}
}

bool rr3SeriesApply(Rr3Career& c, Rr3Texts& texts, const Rr3SeriesJob& job, int32_t& seriesId, std::string& report) {
    auto say = [&](const std::string& s) { report += s + "\n"; };
    std::set<std::string> trackNames;
    for (const Rr3Fields& t : c.tracks) trackNames.insert(t[0].s);
    for (const Rr3SeriesEvent& e : job.events)
        if (!trackNames.count(e.track)) { say("Stopped: \"" + e.track + "\" is not one of the game's track names."); return false; }

    int32_t maxSeries = -1, maxTier = -1, maxEvent = -1;
    for (const Rr3Series& s : c.series) maxSeries = std::max(maxSeries, s.id());
    for (const Rr3Tier& t : c.tiers) {
        maxTier = std::max(maxTier, t.id());
        for (const Rr3Event& e : t.events) maxEvent = std::max(maxEvent, e.id());
    }

    // ---- which series: an existing one, or a copy of the template
    int si = -1;
    bool fresh = job.seriesId < 0;
    if (!fresh) {
        for (size_t k = 0; k < c.series.size(); ++k) if (c.series[k].id() == job.seriesId) si = (int)k;
        if (si < 0) { say("Stopped: there is no series " + std::to_string(job.seriesId) + "."); return false; }
    } else {
        int ti = -1;
        for (size_t k = 0; k < c.series.size(); ++k) if (c.series[k].id() == job.templateId) ti = (int)k;
        if (ti < 0) { say("Stopped: the series to copy (" + std::to_string(job.templateId) + ") is not in the career file."); return false; }
        if (job.events.empty()) { say("Stopped: a new series needs at least one event."); return false; }
        Rr3Series s = c.series[ti];
        int32_t nid = maxSeries + 1;
        std::string key = upperSlug(job.title.empty() ? "CUSTOM_" + std::to_string(nid) : job.title) + "_" + std::to_string(nid);
        s.head[0].u = (uint32_t)nid;
        s.head[1].s = "SERIES_TITLE_" + key;
        s.head[2].u = s.type() == 7 ? 0 : s.head[2].u;    // not a limited-time series: no LTS record points at it
        s.head[3].s = "SERIES_DESC_" + key;
        s.requirements.clear();                           // open from the start
        s.tail[3].s = job.completionReward;
        s.tail[4].s = "NO_REWARD";
        c.series.push_back(s);                            // at the end: LTS records count by index
        si = (int)c.series.size() - 1;
        say("New series " + std::to_string(nid) + " (" + s.head[1].s + "), laid out like series " +
            std::to_string(job.templateId) + ".");
    }
    Rr3Series& S = c.series[si];
    seriesId = S.id();
    if (!job.title.empty()) { texts.set(S.titleKey(), job.title); say("Title: \"" + job.title + "\" (" + S.titleKey() + ")."); }
    if (!job.description.empty()) { texts.set(S.descKey(), job.description); say("Description set (" + S.descKey() + ")."); }
    if (!fresh && !job.completionReward.empty()) S.tail[3].s = job.completionReward;

    // ---- its tiers
    std::vector<size_t> tierIx;
    for (size_t k = 0; k < c.tiers.size(); ++k) if (c.tiers[k].seriesId() == seriesId) tierIx.push_back(k);
    std::sort(tierIx.begin(), tierIx.end(), [&](size_t a, size_t b) { return c.tiers[a].indexInSeries() < c.tiers[b].indexInSeries(); });

    if (!job.events.empty()) {
        // the template tier and event: the copied series' first, else the
        // series' own
        int32_t tmplSeries = fresh ? job.templateId : seriesId;
        const Rr3Tier* tmplTier = nullptr;
        for (const Rr3Tier& t : c.tiers)
            if (t.seriesId() == tmplSeries && !t.events.empty() && (!tmplTier || t.indexInSeries() < tmplTier->indexInSeries())) tmplTier = &t;
        if (!tmplTier) { say("Stopped: the template series has no events to copy."); return false; }
        Rr3Tier baseTier = *tmplTier;
        int tiersWanted = 1;
        for (const Rr3SeriesEvent& e : job.events) tiersWanted = std::max(tiersWanted, e.tier + 1);
        // remove the series' old tiers (an edit replaces them), keeping their ids
        std::vector<int32_t> oldIds;
        if (!fresh) {
            for (size_t k : tierIx) oldIds.push_back(c.tiers[k].id());
            std::vector<Rr3Tier> keep;
            for (const Rr3Tier& t : c.tiers) if (t.seriesId() != seriesId) keep.push_back(t);
            c.tiers.swap(keep);
        }
        std::string key = S.titleKey().compare(0, 13, "SERIES_TITLE_") == 0 ? S.titleKey().substr(13) : upperSlug(job.title);
        int made = 0;
        for (int t = 0; t < tiersWanted; ++t) {
            Rr3Tier T = baseTier;
            T.head[2].u = (uint32_t)t;
            T.head[3].u = (uint32_t)(t < (int)oldIds.size() ? oldIds[t] : ++maxTier);
            T.head[4].u = (uint32_t)seriesId;
            T.head[5].u = 0;                 // unlocked by the series, no trophies
            T.head[6].u = 0;
            if (fresh || t >= (int)oldIds.size()) {
                T.head[0].s = "TIER_NAME_NUMBERED";
                T.head[1].s = "GAMETEXT_TIER_NAME_NUMBERED";
            }
            Rr3Event tmplEvent = baseTier.events[0];
            T.events.clear();
            for (const Rr3SeriesEvent& e : job.events) {
                if (e.tier != t) continue;
                Rr3Event E = tmplEvent;
                for (const Rr3Event& cand : baseTier.events) if (cand.type() == e.type) { E = cand; break; }
                E.a[0].u = (uint32_t)e.type;
                E.a[1].u = (uint32_t)++maxEvent;
                E.a[2].s = e.track;
                if (e.laps > 0) E.a[3].u = (uint32_t)e.laps;
                if (e.fame >= 0) E.a[7].u = (uint32_t)e.fame;
                if (e.reward >= 0) E.a[8].u = (uint32_t)e.reward;
                T.events.push_back(E);
                ++made;
            }
            if (!job.cars.empty()) {
                T.cars.clear();
                for (int32_t id : job.cars) T.cars.push_back({ id, (uint8_t)3 });
            }
            c.tiers.push_back(T);
        }
        if (maxEvent >= 100000) say("Warning: event ids reached 100000 - the game warns past that.");
        say(std::to_string(tiersWanted) + " tier(s), " + std::to_string(made) + " event(s).");
    } else if (!job.cars.empty()) {
        for (size_t k : tierIx) {
            c.tiers[k].cars.clear();
            for (int32_t id : job.cars) c.tiers[k].cars.push_back({ id, (uint8_t)3 });
        }
        say("Allowed cars set on " + std::to_string(tierIx.size()) + " tier(s).");
    }
    if (!job.cars.empty()) say(std::to_string(job.cars.size()) + " car(s) allowed.");

    // ---- where it shows: an existing group, or a new one
    if (fresh || !job.group.empty() || !job.newGroupTitle.empty()) {
        std::vector<Rr3CareerGroup> groups = rr3CareerGroups(c);
        Rr3GroupNode* target = nullptr;
        if (!job.newGroupTitle.empty()) {
            // a new group in the Career menu, copied from one of its groups
            Rr3SuperGroup* career = nullptr;
            for (Rr3SuperGroup& g : c.superGroups) if (g.kind == 0) career = &g;
            if (!career) for (Rr3SuperGroup& g : c.superGroups) career = &g;
            if (!career || career->groups.empty()) { say("Stopped: the career menu has no groups to copy."); return false; }
            Rr3GroupNode n;
            for (const Rr3GroupNode& cand : career->groups) if (cand.type == 0) { n = cand; break; }
            if (n.base.empty()) n = career->groups[0];
            n.type = 0;
            n.children.clear();
            n.pages.clear();
            n.type2.clear();
            std::string gk = upperSlug(job.newGroupTitle);
            n.base[0].s = "STREAM_GROUP_" + gk;
            n.base[1].s = "GAMETEXT_STREAM_GROUP_" + gk + "_FLAVOUR";
            if (!job.groupCard.empty()) n.base[3].s = job.groupCard;
            n.base2[0].s = "";                            // no locked message
            texts.set(n.base[0].s, job.newGroupTitle);
            texts.set(n.base[1].s, job.newGroupFlavour.empty() ? job.newGroupTitle : job.newGroupFlavour);
            career->groups.insert(career->groups.begin(), n);
            target = &career->groups.front();
            say("New career group \"" + job.newGroupTitle + "\" (" + n.base[0].s + "), card " + n.base[3].s + ".");
        } else {
            std::string want = job.group.empty() ? "STREAM_GROUP_AMATEUR" : job.group;
            for (Rr3CareerGroup& g : groups) if (g.key == want && (g.type == 0 || g.type == 1)) target = g.node;
            if (!target) { say("Stopped: no career group " + want + "."); return false; }
        }
        // out of any group it was in, then on a page of its own
        for (Rr3CareerGroup& g : groups)
            for (auto& page : g.node->pages) page.erase(std::remove(page.begin(), page.end(), seriesId), page.end());
        for (Rr3CareerGroup& g : groups) {
            auto& P = g.node->pages;
            P.erase(std::remove_if(P.begin(), P.end(), [](const std::vector<int32_t>& p) { return p.empty(); }), P.end());
        }
        target->pages.push_back({ seriesId });
        say("Listed in " + target->base[0].s + ".");
    }
    return true;
}

// ---- a picture as a new page of a sprite atlas (gui_assets/<res>/<folder>/)

bool rr3AtlasAddPicture(const std::string& atlasXml, const Image& picture, const std::string& imageName,
                        std::string& newXml, Bytes& pageFile, std::string& pageLeaf, std::string& why) {
    // the atlas names its pages sprites_<n>.<format>, <n> the pos id
    size_t fmtAt = atlasXml.find("texture_formats=\"");
    if (fmtAt == std::string::npos) { why = "not a sprite atlas"; return false; }
    fmtAt += 17;
    std::string fmt = atlasXml.substr(fmtAt, atlasXml.find('"', fmtAt) - fmtAt);   // .rgb.pvr
    if (fmt.find(',') != std::string::npos) fmt = fmt.substr(0, fmt.find(','));
    int maxId = -1;
    for (size_t at = 0; (at = atlasXml.find("<pos id=\"", at)) != std::string::npos; at += 9)
        maxId = std::max(maxId, atoi(atlasXml.c_str() + at + 9));
    int id = maxId + 1;
    if (atlasXml.find("name=\"" + imageName + "\"") != std::string::npos) { why = imageName + " is already in the atlas"; return false; }
    Image rgba = toRgba(picture);
    int w = rgba.width, h = rgba.height;
    // the page: a PVR (v2 header) of 32-bit RGBA, as the game's own pages are
    if (fmt != ".rgb.pvr") { why = "atlas pages in " + fmt + " are not written by the tool (only .rgb.pvr)"; return false; }
    Bytes pvr;
    auto put = [&](uint32_t v) { for (int k = 0; k < 4; ++k) pvr.push_back((uint8_t)(v >> (8 * k))); };
    put(0x34); put((uint32_t)h); put((uint32_t)w); put(0); put(0x00010012u); put((uint32_t)(w * h * 4)); put(32);
    put(0); put(0); put(0); put(0); put(0x21525650u); put(1);
    // rows bottom-up, as OpenGL reads them and the game's pages are stored
    for (int y = h - 1; y >= 0; --y)
        pvr.insert(pvr.end(), rgba.pixels.begin() + (size_t)y * w * 4, rgba.pixels.begin() + (size_t)(y + 1) * w * 4);
    pageFile = rr3WrapZ(pvr);
    pageLeaf = "sprites_" + std::to_string(id) + fmt + ".z";
    char entry[512];
    snprintf(entry, sizeof(entry),
             "  <image name=\"%s\" ver=\"0\" width=\"%d\" height=\"%d\">\n    <pos id=\"%d\" x=\"0\" y=\"0\"/>\n"
             "    <hash val=\"MonkeyToolCustomImage%d\"/>\n  </image>\n", imageName.c_str(), w, h, id, id);
    size_t end = atlasXml.rfind("</atlas_images>");
    if (end == std::string::npos) { why = "the atlas has no closing tag"; return false; }
    newXml = atlasXml.substr(0, end) + entry + atlasXml.substr(end);
    return true;
}

// ---- the whole job on the game's files

namespace {
std::string lowerOf(std::string s) { for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a'); return s; }
std::string slashes(std::string s) { for (char& ch : s) if (ch == '\\') ch = '/'; return s; }
// the depot-relative path of an asset: from the first of the game's top folders
std::string depotRel(const std::string& a) {
    // from the leftmost of the game's top folders (Tracks/data/x keeps Tracks/)
    std::string p = slashes(a), l = lowerOf(p);
    static const char* tops[] = { "data/", "gui_assets/", "vehicles/", "tracks/", "gui/" };
    size_t best = std::string::npos;
    for (const char* top : tops) {
        std::string t = top;
        if (l.compare(0, t.size(), t) == 0) best = 0;
        size_t at = l.find("/" + t);
        if (at != std::string::npos && (best == std::string::npos || at + 1 < best)) best = at + 1;
    }
    return best == std::string::npos ? baseName(p) : p.substr(best);
}
std::string findEnding(const std::vector<std::string>& assets, const std::string& tail) {
    std::string t = lowerOf(tail);
    for (const std::string& a : assets) {
        std::string l = lowerOf(slashes(a));
        if (l == t || (l.size() > t.size() && l.compare(l.size() - t.size(), t.size(), t) == 0 && l[l.size() - t.size() - 1] == '/'))
            return a;
    }
    return std::string();
}
}

bool rr3SeriesBuild(const Rr3SeriesJob& jobIn, const std::vector<std::string>& assets,
                    const std::function<bool(const std::string&, Bytes&)>& read,
                    std::vector<Rr3CarOutput>& out, std::string& report, int32_t* madeId) {
    out.clear();
    report.clear();
    Rr3SeriesJob job = jobIn;
    std::string ev = findEnding(assets, "data/events.dat.nct");
    if (ev.empty()) { report = "data/events.dat.nct is not among the files opened."; return false; }
    Bytes raw, plain;
    if (!read(ev, raw)) { report = "events.dat.nct could not be read."; return false; }
    nctTransform(raw.data(), raw.size(), plain, nullptr);
    Rr3Career c;
    std::string why;
    if (!rr3CareerRead(plain.data(), plain.size(), c, why)) { report = why; return false; }
    if (rr3CareerWrite(c) != plain) { report = "events.dat did not read back byte for byte - not touched."; return false; }
    Rr3Texts texts;
    if (!texts.load(assets, read)) report += "gametext.txt is not among the files opened - titles stay as their keys.\n";

    // the new group's card: a page added to the events/career atlas of
    // every screen size opened
    std::vector<std::pair<std::string, Bytes>> atlasFiles;
    if (!job.newGroupTitle.empty() && !job.cardPicture.empty()) {
        Bytes pb;
        Image img;
        if (!readFile(job.cardPicture, pb) || !decodeImageFile(pb.data(), pb.size(), img)) {
            report += "Stopped: the card picture " + baseName(job.cardPicture) + " could not be read.\n";
            return false;
        }
        std::string slug;
        for (char ch : lowerOf(job.newGroupTitle)) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) slug += ch;
            else if (!slug.empty() && slug.back() != '_') slug += '_';
        }
        while (!slug.empty() && slug.back() == '_') slug.pop_back();
        std::string imageName = "stream_group_" + slug + "_card.png";
        int done = 0;
        for (const std::string& a : assets) {
            std::string r = depotRel(a), l = lowerOf(r);
            static const std::string kAtlas = "events/career/sprites.android.atlas";
            if (l.size() < kAtlas.size() || l.compare(l.size() - kAtlas.size(), kAtlas.size(), kAtlas) != 0) continue;
            Bytes xml;
            if (!read(a, xml)) continue;
            // the card at the size the atlas uses for its others
            std::string X(xml.begin(), xml.end());
            int cw = 340, ch = 471;
            size_t at = X.find("_card.png\" ver=\"0\" width=\"");
            if (at != std::string::npos) { cw = atoi(X.c_str() + at + 26); size_t hh = X.find("height=\"", at); if (hh != std::string::npos) ch = atoi(X.c_str() + hh + 8); }
            Image use = toRgba(img);
            if (cw > 16 && ch > 16 && (use.width != cw || use.height != ch)) use = resizeRgba(use, cw, ch);
            std::string newXml, leaf, w2;
            Bytes page;
            if (!rr3AtlasAddPicture(X, use, imageName, newXml, page, leaf, w2)) { report += "Card: " + r + ": " + w2 + "\n"; continue; }
            std::string dir = r.substr(0, r.size() - strlen("sprites.android.atlas"));
            atlasFiles.push_back({ r, Bytes(newXml.begin(), newXml.end()) });
            atlasFiles.push_back({ dir + leaf, page });
            ++done;
        }
        if (!done) report += "Card: no gui_assets/<size>/events/career/sprites.android.atlas among the files opened - the group keeps the copied card.\n";
        else { job.groupCard = "events/career/" + imageName; report += "Card: " + imageName + " added to " + std::to_string(done) + " screen size(s).\n"; }
    }

    int32_t id = -1;
    std::string r;
    if (!rr3SeriesApply(c, texts, job, id, r)) { report += r; return false; }
    report += r;
    if (madeId) *madeId = id;
    Bytes newPlain = rr3CareerWrite(c);
    Rr3Career check;
    if (!rr3CareerRead(newPlain.data(), newPlain.size(), check, why)) { report += "Stopped: the new career file did not read back (" + why + ")."; return false; }
    Bytes enc;
    nctEncode(newPlain.data(), newPlain.size(), enc);
    out.push_back({ depotRel(ev), enc, "career" });
    for (const auto& f : texts.changedFiles()) out.push_back({ f.first, f.second, "text" });
    for (const auto& f : atlasFiles) out.push_back({ f.first, f.second, "card" });

    // the asset list: new files listed; changed ones keep the official checksum
    std::string listAsset = findEnding(assets, "asset_list_base.txtCache.txt");
    Bytes lr;
    Rr3AssetList AL;
    if (!listAsset.empty() && read(listAsset, lr) && rr3AssetListRead(lr, AL)) {
        int added = 0;
        for (const Rr3CarOutput& o : out) {
            if (o.path.find('/') == std::string::npos && lowerOf(o.path) != "gametext.txt") continue;
            size_t before = AL.entries.size();
            uint8_t d[16];
            saves_md5(o.data, d);
            static const char* hx = "0123456789abcdef";
            std::string m;
            for (uint8_t b : d) { m += hx[b >> 4]; m += hx[b & 15]; }
            rr3AssetListSet(AL, o.path, m, true);
            if (AL.entries.size() > before) ++added;
        }
        if (added) { out.push_back({ "asset_list_base.txtCache.txt", rr3AssetListWrite(AL), "asset list" }); report += std::to_string(added) + " new file(s) added to the asset list.\n"; }
    }
    return true;
}

// ---- a new track: a variant of one of the game's, with its own id, name,
// place in the career's track list and (optionally) its own copy of the
// donor's folder for its own models and textures

namespace {
const uint32_t kTkId = 0x9acfeddd, kTkName = 0x99d35de4, kTkNameKey = 0xd4533915, kTkLocKey = 0x9103eb0b,
               kTkFolder = 0xb8fb40ed, kTkCountry = 0xb2172fc9, kTkBase = 0xf8197f89, kTkLayout = 0x16471b2a,
               kTkTime = 0x13ed3c91;
struct TrackKV { std::vector<std::pair<uint32_t, std::string>> kv; };
bool readTrack(const Bytes& p, TrackKV& t) {
    size_t i = 0;
    while (i + 8 <= p.size()) {
        uint32_t h = (uint32_t)p[i] | (uint32_t)p[i + 1] << 8 | (uint32_t)p[i + 2] << 16 | (uint32_t)p[i + 3] << 24;
        uint32_t L = (uint32_t)p[i + 4] | (uint32_t)p[i + 5] << 8 | (uint32_t)p[i + 6] << 16 | (uint32_t)p[i + 7] << 24;
        if (L > p.size() - i - 8) return false;
        t.kv.push_back({ h, std::string((const char*)&p[i + 8], L) });
        i += 8 + L;
    }
    return i == p.size() && !t.kv.empty();
}
Bytes writeTrack(const TrackKV& t) {
    Wr w;
    for (const auto& e : t.kv) { w.u32(e.first); w.str(e.second); }
    return w.b;
}
std::string* trackVal(TrackKV& t, uint32_t h) { for (auto& e : t.kv) if (e.first == h) return &e.second; return nullptr; }
}

std::vector<Rr3TrackInfo> rr3ListTracks(const std::vector<std::string>& assets,
                                        const std::function<bool(const std::string&, Bytes&)>& read) {
    std::vector<Rr3TrackInfo> out;
    for (const std::string& a : assets) {
        std::string r = depotRel(a), l = lowerOf(r);
        if (l.compare(0, 12, "tracks/data/") != 0 || l.size() < 20 || l.compare(l.size() - 8, 8, ".bin.nct") != 0) continue;
        Bytes raw, p;
        if (!read(a, raw)) continue;
        nctTransform(raw.data(), raw.size(), p, nullptr);
        TrackKV t;
        if (!readTrack(p, t)) continue;
        Rr3TrackInfo T;
        T.file = r;
        if (std::string* v = trackVal(t, kTkId)) T.id = atoi(v->c_str());
        if (std::string* v = trackVal(t, kTkName)) T.name = *v;
        if (std::string* v = trackVal(t, kTkFolder)) T.folder = *v;
        if (std::string* v = trackVal(t, kTkTime)) T.time = *v;
        if (std::string* v = trackVal(t, kTkLayout)) T.layout = *v;
        if (std::string* v = trackVal(t, kTkCountry)) T.country = *v;
        out.push_back(T);
    }
    std::sort(out.begin(), out.end(), [](const Rr3TrackInfo& a, const Rr3TrackInfo& b) { return a.name < b.name; });
    return out;
}

bool rr3AddTrack(const Rr3NewTrackJob& job, const std::vector<std::string>& assets,
                 const std::function<bool(const std::string&, Bytes&)>& read,
                 std::vector<Rr3CarOutput>& out, std::string& report) {
    out.clear();
    report.clear();
    auto say = [&](const std::string& s) { report += s + "\n"; };
    auto fail = [&](const std::string& s) { say("Stopped: " + s); return false; };
    if (job.name.empty()) return fail("the new track needs a name");
    std::vector<Rr3TrackInfo> tracks = rr3ListTracks(assets, read);
    if (tracks.empty()) return fail("no Tracks/data/*.bin.nct among the files opened");
    const Rr3TrackInfo* donor = nullptr;
    int maxId = 0;
    for (const Rr3TrackInfo& t : tracks) {
        maxId = std::max(maxId, t.id);
        if (t.name == job.donor || lowerOf(baseName(t.file)) == lowerOf(job.donor)) donor = &t;
        if (t.name == job.name) return fail("a track named \"" + job.name + "\" is already in the game");
    }
    if (!donor) return fail("no track \"" + job.donor + "\" among the files opened");
    int id = job.id > 0 ? job.id : maxId + 1;
    for (const Rr3TrackInfo& t : tracks) if (t.id == id) return fail("track id " + std::to_string(id) + " is taken");

    Bytes raw, p;
    read(findEnding(assets, donor->file), raw);
    nctTransform(raw.data(), raw.size(), p, nullptr);
    TrackKV T;
    readTrack(p, T);
    std::string slug;
    for (char ch : job.name) {
        if (ch >= 'a' && ch <= 'z') slug += (char)(ch - 'a' + 'A');
        else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) slug += ch;
        else if (!slug.empty() && slug.back() != '_') slug += '_';
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    std::string nameKey = "GAMETEXT_TRACK_NAME_" + slug + "_" + std::to_string(id);
    *trackVal(T, kTkId) = std::to_string(id);
    *trackVal(T, kTkName) = job.name;
    *trackVal(T, kTkNameKey) = nameKey;
    std::string locKey;
    if (!job.location.empty()) { locKey = "GAMETEXT_TRACK_LOCATION_" + slug + "_" + std::to_string(id); *trackVal(T, kTkLocKey) = locKey; }
    if (!job.country.empty()) *trackVal(T, kTkCountry) = job.country;
    if (!job.timeLabel.empty()) *trackVal(T, kTkTime) = job.timeLabel;

    // its own folder: the donor's copied, every path in the data pointing there
    std::string oldFolder = donor->folder, newFolder = oldFolder;
    std::vector<std::pair<std::string, Bytes>> copied;
    if (!job.newFolder.empty()) {
        newFolder = "Tracks/" + job.newFolder;
        std::string from = lowerOf(oldFolder) + "/";
        for (const std::string& a : assets) {
            std::string r = depotRel(a);
            if (lowerOf(r).compare(0, from.size(), from) != 0) continue;
            Bytes b;
            if (!read(a, b)) return fail("could not read " + r);
            copied.push_back({ newFolder + "/" + r.substr(from.size()), b });
        }
        if (copied.empty()) return fail("none of " + oldFolder + "/ is among the files opened - open the game's Tracks folder too");
        for (auto& e : T.kv) {
            size_t k = 0;
            while ((k = e.second.find(oldFolder + "/", k)) != std::string::npos) { e.second.replace(k, oldFolder.size(), newFolder); k += newFolder.size(); }
            if (e.second == oldFolder) e.second = newFolder;
        }
        say("Folder: " + oldFolder + " copied to " + newFolder + " (" + std::to_string(copied.size()) + " files).");
        // the user's models over the copied track's, by mesh name
        if (!job.modelPath.empty()) {
            Bytes src;
            Model m;
            if (!readFile(job.modelPath, src) || !(m = readImportModel(src.data(), src.size(), job.modelPath)).valid)
                say("Model: " + baseName(job.modelPath) + " could not be read.");
            else {
                int used = 0;
                for (auto& f : copied) {
                    std::string l = lowerOf(f.first);
                    if (l.size() < 4 || l.compare(l.size() - 4, 4, ".m3g") != 0 || l.find("minimap") != std::string::npos) continue;
                    Bytes built;
                    std::string r;
                    if (rr3ReplaceMeshes(f.second, m, std::map<std::string, MeshPlacement>(), built, r) && built != f.second) {
                        f.second = built;
                        ++used;
                        say("Model: " + baseName(f.first) + " - " + r.substr(0, r.find('\n')));
                    }
                }
                if (!used) say("Model: none of " + baseName(job.modelPath) + "'s objects is named like a mesh of the track - "
                               "export the track's .m3g from the tool first and keep its mesh names.");
            }
        }
    }

    std::string base = trackVal(T, kTkBase) ? *trackVal(T, kTkBase) : "track";
    std::string layout = trackVal(T, kTkLayout) ? *trackVal(T, kTkLayout) : "circuit";
    std::string time = *trackVal(T, kTkTime);
    std::string dataRel = "Tracks/data/" + base + "-" + layout + "-" + time + "-" + std::to_string(id) + ".bin.nct";
    {
        Bytes plainT = writeTrack(T), enc;
        nctEncode(plainT.data(), plainT.size(), enc);
        out.push_back({ dataRel, enc, "new" });
    }
    say("Track " + std::to_string(id) + " \"" + job.name + "\": " + dataRel + ", copied from " + donor->name + ".");

    // ttk.dat: the collision mesh's checksum under the new id
    std::string ttkAsset = findEnding(assets, "tracks/data/ttk.dat.nct");
    if (ttkAsset.empty()) say("ttk.dat.nct is not among the files opened - the collision check list is not updated.");
    else {
        Bytes tr, tp;
        read(ttkAsset, tr);
        nctTransform(tr.data(), tr.size(), tp, nullptr);
        Rd r(tp.data(), tp.size());
        uint32_t ver = r.u32(), n = r.u32();
        std::vector<std::pair<uint32_t, std::string>> ent;
        for (uint32_t k = 0; k < n && !r.bad; ++k) { uint32_t i = r.u32(); ent.push_back({ i, r.str() }); }
        std::string md;
        for (const auto& e : ent) if ((int)e.first == donor->id) md = e.second;
        if (!job.newFolder.empty()) {
            for (const auto& f : copied) {
                std::string l = lowerOf(f.first);
                if (l.size() > 4 && l.compare(l.size() - 4, 4, ".fmc") == 0 && l.find("/" + lowerOf(layout) + "/") != std::string::npos) {
                    uint8_t d[16];
                    saves_md5(f.second, d);
                    static const char* hx = "0123456789abcdef";
                    md.clear();
                    for (uint8_t b : d) { md += hx[b >> 4]; md += hx[b & 15]; }
                }
            }
        }
        if (r.bad || md.empty()) say("ttk.dat: the donor has no entry - not updated.");
        else {
            ent.push_back({ (uint32_t)id, md });
            Wr w;
            w.u32(ver);
            w.u32((uint32_t)ent.size());
            for (const auto& e : ent) { w.u32(e.first); w.str(e.second); }
            Bytes enc;
            nctEncode(w.b.data(), w.b.size(), enc);
            out.push_back({ depotRel(ttkAsset), enc, "changed" });
            say("ttk.dat: collision checksum listed for track " + std::to_string(id) + ".");
        }
    }

    // the career's track list, so events can use it
    std::string ev = findEnding(assets, "data/events.dat.nct");
    if (ev.empty()) say("data/events.dat.nct is not among the files opened - events cannot use the track until it is in the career's track list.");
    else {
        Bytes er, ep;
        read(ev, er);
        nctTransform(er.data(), er.size(), ep, nullptr);
        Rr3Career c;
        std::string why;
        if (!rr3CareerRead(ep.data(), ep.size(), c, why)) say("Career: " + why + " - not updated.");
        else {
            Rr3Fields entry;
            for (const Rr3Fields& t : c.tracks) if (t[0].s == donor->name) entry = t;
            if (entry.empty()) { entry = c.tracks.empty() ? Rr3Fields() : c.tracks[0]; }
            if (!entry.empty()) {
                entry[0].s = job.name;
                c.tracks.push_back(entry);
                Bytes np = rr3CareerWrite(c), enc;
                nctEncode(np.data(), np.size(), enc);
                out.push_back({ depotRel(ev), enc, "changed" });
                say("Career: \"" + job.name + "\" added to the track list - the series editor can now put events on it.");
            }
        }
    }

    // the name in every language
    Rr3Texts texts;
    if (texts.load(assets, read)) {
        texts.set(nameKey, job.name);
        if (!locKey.empty()) texts.set(locKey, job.location);
        for (const auto& f : texts.changedFiles()) out.push_back({ f.first, f.second, "text" });
        say("Text: " + nameKey + (locKey.empty() ? "" : " and " + locKey) + " in every language.");
    } else say("Text: gametext.txt is not among the files opened - the name shows as its key.");

    for (const auto& f : copied) out.push_back({ f.first, f.second, "new" });

    // the asset list
    std::string listAsset = findEnding(assets, "asset_list_base.txtCache.txt");
    Bytes lr;
    Rr3AssetList AL;
    if (!listAsset.empty() && read(listAsset, lr) && rr3AssetListRead(lr, AL)) {
        int added = 0;
        for (const Rr3CarOutput& o : out) {
            if (o.path.find('/') == std::string::npos && lowerOf(o.path) != "gametext.txt") continue;
            size_t before = AL.entries.size();
            uint8_t d[16];
            saves_md5(o.data, d);
            static const char* hx = "0123456789abcdef";
            std::string m;
            for (uint8_t b : d) { m += hx[b >> 4]; m += hx[b & 15]; }
            rr3AssetListSet(AL, o.path, m, true);
            if (AL.entries.size() > before) ++added;
        }
        out.push_back({ "asset_list_base.txtCache.txt", rr3AssetListWrite(AL), "asset list" });
        say("Asset list: " + std::to_string(added) + " new file(s) listed.");
    }
    return true;
}

} // namespace nfsnl
