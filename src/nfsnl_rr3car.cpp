// nfsnl_rr3car.cpp - Real Racing 3 car data: vehicles/data/<id>_<car>.bin.nct
//
// Under the .nct pad (see nfsnl_rr3.cpp) a car's data file is a plain run of
// little-endian fields, each a 4-byte number or a string (u32 length, then
// the characters, no terminator). The order is the same in all 540 cars of
// the current game, so the fields are found by their place among the strings:
//
//   u32 0 | u32 car id | year | make | model | u32 | class (Race, Supercar,
//   Prod) | 13 numbers: u32, u32, weight (lb), top speed (mph), top speed
//   fully upgraded, u32, u32, 0-60 mph (s) stock and upgraded, 60-0 braking
//   (ft) stock and upgraded, cornering grip (g) stock and upgraded |
//   drivetrain (RWD, AWD, FWD, RLY) | the engine sound: accel and decel
//   sample paths, idle, levels, the bf_ / _ovrun families | transmission and
//   shift sounds | ... | aspiration (Turbo, Nat, Super, None) | engine
//   position (F, M, R, FM) | gears, u32, top gear ratio, final drive, power
//   (hp), power rpm, torque (lb-ft), torque rpm, redline, limiter, two shift
//   points, one ratio per gear, reverse | handling profile (PROD_04, RACE_03
//   or the car's own) | ... | 21-22 numbers ending in the price (R$), the
//   price in gold, ... | CAR_MANUFACTURER_<x> | CAR_NAME_<x> | 3 x u32 |
//   performance rating stock and upgraded | ...
//
// Names were read off cars whose figures are public: the 2010 BMW M3 GTS
// says 3538 lb, 190 mph, 4.4 s, 444 hp at 8300 rpm, 325 lb-ft at 3750 rpm.
#include "nfsnl.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nfsnl {

namespace {

bool printable(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if (p[i] < 32 || p[i] >= 127) return false;
    return true;
}
// a string the layout says must be there: text, line breaks allowed (one
// unlock condition carries a stray newline)
bool texty(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) if ((p[i] < 32 && p[i] != '\n' && p[i] != '\r' && p[i] != '\t') || p[i] >= 127) return false;
    return true;
}

uint32_t rdU32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

std::string fmtFloat(float f) {
    char b[64];
    snprintf(b, sizeof(b), "%.4g", f);
    return b;
}

} // namespace

bool rr3CarRead(const uint8_t* nct, size_t len, Rr3Car& car) {
    car = Rr3Car();
    if (len < 64) { car.why = "too short for a car data file"; return false; }
    size_t covered = 0;
    nctTransform(nct, len, car.plain, &covered);
    car.original.assign(nct, nct + len);
    car.plainOriginal = car.plain;
    if (covered < len) { car.why = "the .nct pad does not reach the end of this file"; return false; }
    const Bytes& p = car.plain;
    size_t i = 0;
    // one field: 'n' a number, 's' a string (an empty one is four zero
    // bytes), '?' whichever it looks like
    auto take = [&](char kind) -> bool {
        if (i + 4 > p.size()) return false;
        uint32_t L = rdU32(&p[i]);
        Rr3Car::Tok t;
        bool str = kind == 's' ? (L <= 400 && i + 4 + L <= p.size() && texty(&p[i + 4], L))
                 : kind == '?' ? (L >= 1 && L <= 160 && i + 4 + L <= p.size() && printable(&p[i + 4], L))
                 : false;
        if (kind == 's' && !str) return false;
        if (str) {
            t.str = true;
            t.s.assign((const char*)&p[i + 4], L);
            i += 4 + L;
        } else {
            memcpy(t.raw, &p[i], 4);
            i += 4;
        }
        car.toks.push_back(t);
        return true;
    };
    // the fixed head: version, id, year, make, model, u32, class, the
    // performance block, drivetrain, the engine sound
    static const char kHead[] = "nnsssns" "nnnnnnnnnnnnn" "s" "sssnnnnssnnns" "s";
    for (const char* k = kHead; *k; ++k)
        if (!take(*k)) { car.why = "not laid out like a car data file"; return false; }
    car.iYear = 2; car.iMake = 3; car.iModel = 4; car.iClass = 6; car.iBlock = 7; car.iDrive = 20;
    car.iSteady = 34;
    // then fields whose kind is read off the bytes, up to the aspiration and
    // engine position; the engine block after them is numbers only
    while (i + 4 <= p.size()) {
        if (!take('?')) break;
        size_t n = car.toks.size();
        if (n >= 2 && car.toks[n - 1].str && car.toks[n - 2].str && car.toks[n - 1].s.size() <= 2 &&
            car.iAsp < 0) {
            const std::string& a2 = car.toks[n - 2].s;
            if (a2 == "Turbo" || a2 == "Nat" || a2 == "Super" || a2 == "None" || a2 == "Electric" || a2 == "Hybrid") {
                car.iAsp = (int)n - 2;
                car.iPos = (int)n - 1;
                if (i + 4 <= p.size()) {
                    uint32_t gears = rdU32(&p[i]);
                    if (gears >= 1 && gears <= 12) {
                        car.iEngine = (int)n;
                        car.gears = (int)gears;
                        bool whole = true;
                        for (uint32_t k = 0; k < 12 + gears + 1 && whole; ++k) whole = take('n');
                        // then two strings, either of which may be empty: the
                        // upgrade / handling package and the unlock condition
                        // (QUEST:..., else "")
                        if (whole && take('s')) {
                            car.iProfile = (int)car.toks.size() - 1;
                            if (take('s')) car.iUnlock = (int)car.toks.size() - 1;
                        }
                    }
                }
            }
        }
    }
    car.tail.assign(p.begin() + i, p.end());     // fewer than four stray bytes
    // the strings, in order
    std::vector<int> S;
    for (size_t k = 0; k < car.toks.size(); ++k) if (car.toks[k].str) S.push_back((int)k);
    for (int k : S) {
        const std::string& s = car.toks[k].s;
        if (car.iMakeKey < 0 && s.compare(0, 17, "CAR_MANUFACTURER_") == 0) car.iMakeKey = k;
        if (car.iNameKey < 0 && s.compare(0, 9, "CAR_NAME_") == 0) car.iNameKey = k;
    }
    if (car.iProfile >= 0 && car.iProfile + 11 < (int)car.toks.size() && !car.toks[car.iProfile + 11].str) {
        // the package, the unlock condition, three numbers, the appearance
        // index (which model folder), five more, the price
        car.iPrice = car.iProfile + 11;
    } else if (car.iMakeKey >= 0) {
        // the price block: the numbers just before the make key
        int k = car.iMakeKey - 1;
        while (k > 0 && !car.toks[k].str) --k;
        int count = car.iMakeKey - 1 - k;
        if (count >= 12) { car.iPrice = car.iMakeKey - 12; }
    }
    if (car.iNameKey >= 0 && car.iNameKey + 5 < (int)car.toks.size() &&
        !car.toks[car.iNameKey + 4].str && !car.toks[car.iNameKey + 5].str)
        car.iPr = car.iNameKey + 4;
    car.ok = car.iSteady < (int)car.toks.size() && car.toks[car.iSteady].str;
    if (!car.ok) car.why = "the engine sound fields were not found";
    return car.ok;
}

namespace {
struct FieldDef { const char* key; const char* label; char kind; int base; int off; };
// kind: s string, u u32, f float. base: which anchor the offset counts from.
enum { B_ID, B_YEAR, B_MAKE, B_MODEL, B_CLASS, B_BLOCK, B_DRIVE, B_ASP, B_POS, B_ENGINE, B_PROFILE,
       B_PRICE, B_MAKEKEY, B_NAMEKEY, B_PR };
const FieldDef kFields[] = {
    { "id",               "Car id (the number in the data file's name)", 'u', B_ID, 0 },
    { "year",             "Year",                                   's', B_YEAR, 0 },
    { "make",             "Make",                                   's', B_MAKE, 0 },
    { "model",            "Model",                                  's', B_MODEL, 0 },
    { "class",            "Class (Race, Supercar, Prod)",           's', B_CLASS, 0 },
    { "weight_lb",        "Weight (lb)",                            'u', B_BLOCK, 2 },
    { "top_speed",        "Top speed (mph)",                        'u', B_BLOCK, 3 },
    { "top_speed_max",    "Top speed, fully upgraded (mph)",        'u', B_BLOCK, 4 },
    { "accel",            "0-60 mph (s)",                           'f', B_BLOCK, 7 },
    { "accel_max",        "0-60 mph, fully upgraded (s)",           'f', B_BLOCK, 8 },
    { "braking",          "60-0 mph braking (ft)",                  'f', B_BLOCK, 9 },
    { "braking_max",      "Braking, fully upgraded (ft)",           'f', B_BLOCK, 10 },
    { "grip",             "Cornering grip (g)",                     'f', B_BLOCK, 11 },
    { "grip_max",         "Grip, fully upgraded (g)",               'f', B_BLOCK, 12 },
    { "drivetrain",       "Drivetrain (RWD, AWD, FWD, RLY)",        's', B_DRIVE, 0 },
    { "aspiration",       "Aspiration (Turbo, Nat, Super, None)",   's', B_ASP, 0 },
    { "layout",           "Engine position (F, M, R, FM)",          's', B_POS, 0 },
    { "gears",            "Gears",                                  'u', B_ENGINE, 0 },
    { "final_drive",      "Final drive ratio",                      'f', B_ENGINE, 3 },
    { "power_hp",         "Power (hp)",                             'f', B_ENGINE, 4 },
    { "power_rpm",        "Power at (rpm)",                         'u', B_ENGINE, 5 },
    { "torque",           "Torque (lb-ft)",                         'u', B_ENGINE, 6 },
    { "torque_rpm",       "Torque at (rpm)",                        'u', B_ENGINE, 7 },
    { "redline",          "Redline (rpm)",                          'u', B_ENGINE, 8 },
    { "limiter",          "Limiter (rpm)",                          'u', B_ENGINE, 9 },
    { "shift_up",         "Shift up at (rpm)",                      'u', B_ENGINE, 10 },
    { "shift_down",       "Shift down at (rpm)",                    'u', B_ENGINE, 11 },
    { "handling",         "Upgrade / handling package (another car's, e.g. SUPER_03)", 's', B_PROFILE, 0 },
    { "unlock",           "Unlock condition (QUEST:..., empty for the dealer)", 's', B_PROFILE, 1 },
    { "appearance",       "Appearance (which model folder the car uses)", 'u', B_PROFILE, 5 },
    { "price",            "Price (R$)",                             'u', B_PRICE, 0 },
    { "price_gold",       "Price (gold)",                           'u', B_PRICE, 1 },
    { "make_key",         "Make text key (CAR_MANUFACTURER_...)",   's', B_MAKEKEY, 0 },
    { "name_key",         "Name text key (CAR_NAME_...)",           's', B_NAMEKEY, 0 },
    { "pr",               "Performance rating (PR)",                'f', B_PR, 0 },
    { "pr_max",           "PR, fully upgraded",                     'f', B_PR, 1 },
};

int anchor(const Rr3Car& c, int base) {
    switch (base) {
    case B_ID: return c.iYear >= 0 ? 1 : -1;
    case B_YEAR: return c.iYear;
    case B_MAKE: return c.iMake;
    case B_MODEL: return c.iModel;
    case B_CLASS: return c.iClass;
    case B_BLOCK: return c.iBlock;
    case B_DRIVE: return c.iDrive;
    case B_ASP: return c.iAsp;
    case B_POS: return c.iPos;
    case B_ENGINE: return c.iEngine;
    case B_PROFILE: return c.iProfile;
    case B_PRICE: return c.iPrice;
    case B_MAKEKEY: return c.iMakeKey;
    case B_NAMEKEY: return c.iNameKey;
    case B_PR: return c.iPr;
    }
    return -1;
}

const FieldDef* findField(const std::string& key) {
    for (const FieldDef& f : kFields) if (key == f.key) return &f;
    return nullptr;
}
} // namespace

std::vector<Rr3CarField> rr3CarFields(const Rr3Car& car) {
    std::vector<Rr3CarField> out;
    for (const FieldDef& f : kFields) {
        int a = anchor(car, f.base);
        if (a < 0) continue;
        int i = a + f.off;
        if (i < 0 || i >= (int)car.toks.size()) continue;
        const Rr3Car::Tok& t = car.toks[i];
        if ((f.kind == 's') != t.str) continue;
        Rr3CarField o;
        o.key = f.key;
        o.label = f.label;
        o.text = f.kind == 's';
        if (f.kind == 's') o.value = t.s;
        else if (f.kind == 'u') o.value = std::to_string(rdU32(t.raw));
        else { float v; memcpy(&v, t.raw, 4); o.value = fmtFloat(v); }
        out.push_back(o);
    }
    // the gear ratios, one per gear
    if (car.iEngine >= 0) {
        std::string r;
        for (int g = 0; g < car.gears; ++g) {
            int i = car.iEngine + 12 + g;
            if (i >= (int)car.toks.size() || car.toks[i].str) { r.clear(); break; }
            float v;
            memcpy(&v, car.toks[i].raw, 4);
            r += (g ? ", " : "") + fmtFloat(v);
        }
        if (!r.empty()) out.push_back({ "gear_ratios", "Gear ratios, first to top", r, false });
    }
    // the engine sound: its family, as the sample folder names it
    {
        std::string fam;
        for (int i = car.iDrive + 1; i < car.iSteady && fam.empty(); ++i)
            if (car.toks[i].str && car.toks[i].s.compare(0, 15, "audio/Vehicles/") == 0) {
                std::string s = car.toks[i].s.substr(15);
                fam = s.substr(0, s.find('/'));
            }
        if (fam.empty() && car.toks[car.iDrive + 13].str) fam = car.toks[car.iDrive + 13].s;
        if (fam.empty() && car.toks[car.iDrive + 8].str) fam = car.toks[car.iDrive + 8].s;
        if (!fam.empty()) out.push_back({ "sound", "Engine sound (samples)", fam, true });
    }
    return out;
}

bool rr3CarSet(Rr3Car& car, const std::string& key, const std::string& value, std::string& why) {
    if (key == "gear_ratios") {
        std::vector<float> r;
        const char* s = value.c_str();
        while (*s) {
            char* e = nullptr;
            float v = strtof(s, &e);
            if (e == s) { ++s; continue; }
            r.push_back(v);
            s = e;
        }
        if ((int)r.size() != car.gears || car.iEngine < 0) {
            why = "give one ratio per gear (" + std::to_string(car.gears) + ")";
            return false;
        }
        for (int g = 0; g < car.gears; ++g) memcpy(car.toks[car.iEngine + 12 + g].raw, &r[g], 4);
        memcpy(car.toks[car.iEngine + 2].raw, &r.back(), 4);   // the top gear's, kept beside the count
        return true;
    }
    const FieldDef* f = findField(key);
    if (!f) { why = "no field called " + key; return false; }
    int a = anchor(car, f->base);
    int i = a < 0 ? -1 : a + f->off;
    if (i < 0 || i >= (int)car.toks.size() || (f->kind == 's') != car.toks[i].str) {
        why = std::string("this car has no ") + f->label;
        return false;
    }
    Rr3Car::Tok& t = car.toks[i];
    // The game reads these by their exact word (all 540 cars use only these):
    // other spellings are taken as the word they mean, anything else refused
    // - "normal" written as it was left the car without an aspiration the
    // game knows.
    std::string val = value;
    {
        struct Alias { const char* key; const char* word; std::vector<const char*> also; };
        static const Alias kAliases[] = {
            { "aspiration", "Nat",   { "nat", "na", "n/a", "normal", "natural", "naturally aspirated", "atmo", "atmospheric" } },
            { "aspiration", "Turbo", { "turbo", "turbocharged", "t", "biturbo", "twin turbo", "twin-turbo" } },
            { "aspiration", "Super", { "super", "supercharged", "supercharger", "sc", "compressor", "kompressor" } },
            { "aspiration", "None",  { "none", "electric", "ev", "hybrid-electric", "" } },
            { "drivetrain", "RWD",   { "rwd", "rear", "rear-wheel drive", "rear wheel drive" } },
            { "drivetrain", "FWD",   { "fwd", "front", "front-wheel drive", "front wheel drive" } },
            { "drivetrain", "AWD",   { "awd", "4wd", "4x4", "all", "all-wheel drive", "all wheel drive" } },
            { "drivetrain", "RLY",   { "rly", "rally" } },
            { "layout", "F",  { "f", "front" } },
            { "layout", "FM", { "fm", "front-mid", "front mid" } },
            { "layout", "M",  { "m", "mid", "middle" } },
            { "layout", "R",  { "r", "rear" } },
            { "class", "Prod",     { "prod", "production", "road" } },
            { "class", "Supercar", { "supercar", "super car", "hypercar" } },
            { "class", "Race",     { "race", "racing", "racer" } },
        };
        bool enumerated = false, found = false;
        std::string low = value;
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        while (!low.empty() && low.back() == ' ') low.pop_back();
        while (!low.empty() && low.front() == ' ') low.erase(0, 1);
        std::string words;
        for (const Alias& a : kAliases) {
            if (key != a.key) continue;
            enumerated = true;
            words += words.empty() ? a.word : std::string(", ") + a.word;
            for (const char* w : a.also)
                if (low == w) { val = a.word; found = true; }
        }
        if (enumerated && !found) {
            why = std::string(f->label) + ": one of " + words;
            return false;
        }
    }
    if (f->kind == 's') {
        const std::string& value = val;
        bool mayBeEmpty = key == "unlock" || key == "handling";
        if ((value.empty() && !mayBeEmpty) || value.size() > 400) { why = std::string(f->label) + ": 1 to 400 characters"; return false; }
        for (char c : value) if ((uint8_t)c < 32 || (uint8_t)c >= 127) { why = std::string(f->label) + ": plain ASCII only"; return false; }
        t.s = value;
    } else if (f->kind == 'u') {
        char* e = nullptr;
        unsigned long v = strtoul(value.c_str(), &e, 10);
        if (!e || *e) { why = std::string(f->label) + ": a whole number"; return false; }
        if (key == "gears" && (int)v != car.gears) { why = "the number of gears follows the gear ratios"; return false; }
        uint32_t u = (uint32_t)v;
        memcpy(t.raw, &u, 4);
    } else {
        char* e = nullptr;
        float v = strtof(value.c_str(), &e);
        if (!e || *e || !std::isfinite(v)) { why = std::string(f->label) + ": a number"; return false; }
        memcpy(t.raw, &v, 4);
    }
    return true;
}

bool rr3CarCopySound(Rr3Car& car, const Rr3Car& from, std::string& why) {
    if (!car.ok || !from.ok) { why = "both cars must be read first"; return false; }
    // from the drivetrain to the transmission sounds: the samples, the idle,
    // their levels and the bf_ / _ovrun families
    std::vector<Rr3Car::Tok> mid(from.toks.begin() + from.iDrive + 1, from.toks.begin() + from.iSteady);
    int shift = (int)mid.size() - (car.iSteady - car.iDrive - 1);
    car.toks.erase(car.toks.begin() + car.iDrive + 1, car.toks.begin() + car.iSteady);
    car.toks.insert(car.toks.begin() + car.iDrive + 1, mid.begin(), mid.end());
    for (int* p : { &car.iSteady, &car.iAsp, &car.iPos, &car.iEngine, &car.iProfile, &car.iUnlock, &car.iPrice,
                    &car.iMakeKey, &car.iNameKey, &car.iPr })
        if (*p > car.iDrive) *p += shift;
    return true;
}

Bytes rr3CarPlain(const Rr3Car& car) {
    Bytes p;
    p.reserve(car.plain.size() + 64);
    for (const Rr3Car::Tok& t : car.toks) {
        if (t.str) {
            uint32_t n = (uint32_t)t.s.size();
            for (int k = 0; k < 4; ++k) p.push_back((uint8_t)(n >> (8 * k)));
            p.insert(p.end(), t.s.begin(), t.s.end());
        } else {
            p.insert(p.end(), t.raw, t.raw + 4);
        }
    }
    p.insert(p.end(), car.tail.begin(), car.tail.end());
    return p;
}

bool rr3CarWrite(const Rr3Car& car, Bytes& out, std::string& report) {
    Bytes plain = rr3CarPlain(car);
    Bytes enc;
    size_t uncertain = 0, past = 0;
    nctEncode(plain.data(), plain.size(), enc, &uncertain, &past);
    // where a byte is the same as the original's at the same place, the
    // original's encoded byte is exact whatever the pad says
    size_t risky = 0;
    for (size_t i = 0; i < plain.size(); ++i) {
        if (i < car.plainOriginal.size() && plain[i] == car.plainOriginal[i]) { enc[i] = car.original[i]; continue; }
        if (!nctPadByteKnown(i)) ++risky;
    }
    out.swap(enc);
    char b[200];
    snprintf(b, sizeof(b), "%zu bytes (was %zu); %zu changed byte(s) fall where the .nct pad is not certain",
             out.size(), car.original.size(), risky);
    report = b;
    return risky == 0 && past == 0;
}

} // namespace nfsnl

namespace nfsnl {

namespace {
std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }
std::string leafOf(const std::string& p) { size_t s = p.find_last_of("/\\"); return s == std::string::npos ? p : p.substr(s + 1); }
// a texture's stem: 2011_pagani_huayra_ext_black.etc.dds.z -> 2011_pagani_huayra_ext_black
std::string texStem(const std::string& leaf) { size_t d = leaf.find('.'); return lower(d == std::string::npos ? leaf : leaf.substr(0, d)); }
bool isGameTexture(const std::string& leaf) {
    std::string l = lower(leaf);
    return l.find(".etc.dds") != std::string::npos || l.find(".pvr") != std::string::npos ||
           l.find(".atc.dds") != std::string::npos || l.find(".dds") != std::string::npos;
}
}

std::string rr3CarDataPathFor(const std::string& car, const std::vector<std::string>& assets) {
    std::string want = "_" + lower(car) + ".bin.nct";
    for (const std::string& a : assets) {
        std::string l = lower(a);
        if (l.size() > want.size() && l.compare(l.size() - want.size(), want.size(), want) == 0 &&
            l.find("liveries") == std::string::npos) {
            // <id>_<car>.bin.nct: the part before must be the id
            std::string leaf = leafOf(l);
            size_t us = leaf.find('_');
            bool digits = us != std::string::npos && us > 0;
            for (size_t k = 0; digits && k < us; ++k) if (leaf[k] < '0' || leaf[k] > '9') digits = false;
            if (digits && leaf.substr(us) == want) return a;
        }
    }
    return std::string();
}

bool rr3CreateCar(const Rr3CarJob& job, const std::vector<std::string>& assets,
                  const std::function<bool(const std::string&, Bytes&)>& readAsset,
                  std::vector<Rr3CarOutput>& out, std::string& report) {
    out.clear();
    report.clear();
    std::string car = lower(job.car);
    bool fine = true;
    auto say = [&](const std::string& s) { report += s + "\n"; };

    // 1. the data file
    std::string dataPath = rr3CarDataPathFor(car, assets);
    if (!job.fields.empty() || !job.soundFrom.empty()) {
        Bytes raw;
        Rr3Car c;
        if (dataPath.empty() || !readAsset(dataPath, raw)) {
            say("Data: no <id>_" + car + ".bin.nct in the game's vehicles/data - the car's figures were left alone.");
            fine = false;
        } else if (!rr3CarRead(raw.data(), raw.size(), c)) {
            say("Data: " + leafOf(dataPath) + " could not be read: " + c.why);
            fine = false;
        } else {
            std::string why;
            bool ok = true;
            for (const auto& kv : job.fields) {
                if (kv.first == "id") continue;          // the car keeps its place in the game
                bool same = false;
                for (const Rr3CarField& f : rr3CarFields(c))
                    if (f.key == kv.first && f.value == kv.second) same = true;
                if (same) continue;
                if (!rr3CarSet(c, kv.first, kv.second, why)) { say("Data: " + why); ok = false; }
            }
            if (ok && !job.soundFrom.empty()) {
                Rr3Car from;
                if (!rr3CarRead(job.soundFrom.data(), job.soundFrom.size(), from) || !rr3CarCopySound(c, from, why)) {
                    say("Data: the engine sound could not be copied: " + (why.empty() ? from.why : why));
                    ok = false;
                }
            }
            Bytes enc;
            std::string r;
            if (ok && rr3CarWrite(c, enc, r)) {
                if (enc != raw) {
                    out.push_back({ dataPath, enc, "the car's figures" });
                    say("Data: " + leafOf(dataPath) + " - " + r);
                } else {
                    say("Data: no figure changed.");
                }
            } else {
                if (ok) say("Data: " + leafOf(dataPath) + " not written - " + r +
                            ". Keep names the same length, or edit fewer strings, so the file does not move onto them.");
                fine = false;
            }
        }
    }

    // 2. the models: <car>_a.m3g from the exterior, <car>_int_a.m3g from the cockpit
    auto model = [&](const std::string& src, const std::string& leaf) {
        if (src.empty()) return;
        std::string target;
        for (const std::string& a : assets) if (lower(leafOf(a)) == leaf) { target = a; break; }
        if (target.empty()) { say("Model: " + leaf + " is not in the game - " + leafOf(src) + " was not used."); fine = false; return; }
        Bytes tmpl, srcBytes;
        if (!readAsset(target, tmpl) || !readFile(src, srcBytes)) { say("Model: could not read " + leafOf(src) + " or " + leaf); fine = false; return; }
        Model m = readImportModel(srcBytes.data(), srcBytes.size(), src);
        if (!m.valid) { say("Model: " + leafOf(src) + " has no meshes"); fine = false; return; }
        std::vector<NameFix> fixes = rr3SuggestNames(tmpl, m, true);
        rr3ApplyNameFixes(m, fixes);
        Bytes built;
        std::string r;
        if (!rr3BuildCar(tmpl, m, built, r)) { say("Model: " + leaf + " could not be built: " + r); fine = false; return; }
        out.push_back({ target, built, "built from " + leafOf(src) });
        std::string first = r.substr(0, r.find('\n'));
        say("Model: " + leaf + " from " + leafOf(src) + (fixes.empty() ? "" : " (" + std::to_string(fixes.size()) + " name(s) matched to the car's)") + " - " + first);
    };
    model(job.modelPath, car + "_a.m3g");
    model(job.interiorPath, car + "_int_a.m3g");

    // 3. textures: a picture named like one of the car's textures, with or
    // without the car's name in front (misc.png, ext_black.png, or
    // 2011_pagani_huayra_misc.png). ext.png paints every livery.
    if (!job.pictures.empty()) {
        std::vector<std::string> mine;          // the car's own textures
        std::string folder = "vehicles/" + car + "/";
        for (const std::string& a : assets) {
            std::string l = lower(a);
            size_t at = l.find(folder);
            if (at == std::string::npos || !isGameTexture(leafOf(l))) continue;
            if (l.find("shadows") != std::string::npos) continue;
            mine.push_back(a);
        }
        int used = 0;
        for (const std::string& pic : job.pictures) {
            std::string stem = texStem(leafOf(pic));
            if (stem.compare(0, car.size() + 1, car + "_") == 0) stem = stem.substr(car.size() + 1);
            std::vector<std::string> targets;
            for (const std::string& t : mine) {
                std::string ts = texStem(leafOf(t));
                if (ts.compare(0, car.size() + 1, car + "_") == 0) ts = ts.substr(car.size() + 1);
                if (ts == stem || (stem == "ext" && ts.compare(0, 4, "ext_") == 0) || (stem == "ext" && ts == "ext"))
                    targets.push_back(t);
            }
            if (targets.empty()) { say("Texture: " + leafOf(pic) + " matches none of the car's textures - skipped."); continue; }
            Bytes picBytes;
            Image img;
            if (!readFile(pic, picBytes) || !decodeImageFile(picBytes.data(), picBytes.size(), img)) {
                say("Texture: " + leafOf(pic) + " could not be read."); fine = false; continue;
            }
            for (const std::string& t : targets) {
                Bytes orig;
                Image cur;
                Bytes inner;
                std::string innerName;
                size_t frames = 0;
                if (readAsset(t, orig) && rr3Unwrap(leafOf(t), orig.data(), orig.size(), inner, &innerName, &frames) &&
                    !inner.empty()) {
                } else {
                    inner = orig;
                }
                if (orig.empty() || !decodeTextureFile(inner.data(), inner.size(), cur)) {
                    say("Texture: " + leafOf(t) + " could not be read."); fine = false; continue;
                }
                Image use = toRgba(img);
                if (use.width != cur.width || use.height != cur.height) use = resizeRgba(use, cur.width, cur.height);
                Bytes enc;
                std::string r;
                bool wrapped = lower(leafOf(t)).size() > 2 && lower(leafOf(t)).compare(lower(leafOf(t)).size() - 2, 2, ".z") == 0;
                std::string innerLeaf = innerName.empty() ? leafOf(t) : innerName;
                if (!importPicture(wrapped ? innerLeaf : t, wrapped ? inner : orig, use, enc, r)) {
                    say("Texture: " + leafOf(t) + ": " + r); fine = false; continue;
                }
                if (wrapped) enc = rr3WrapZ(enc);
                out.push_back({ t, enc, "from " + leafOf(pic) });
                ++used;
                say("Texture: " + leafOf(t) + " from " + leafOf(pic) +
                    (img.width != cur.width || img.height != cur.height
                         ? " (resized to " + std::to_string(cur.width) + "x" + std::to_string(cur.height) + ")" : ""));
            }
        }
        if (!used) say("Texture: none of the pictures was used.");
    }
    // 4. more liveries
    if (!job.liveries.empty()) {
        std::vector<Rr3CarOutput> lo;
        std::string r;
        if (!rr3AddLiveries(car, job.liveries, assets, readAsset, lo, r)) fine = false;
        report += r;
        for (Rr3CarOutput& o : lo) {
            // a file the steps above changed too (the asset list) is replaced
            bool merged = false;
            for (Rr3CarOutput& e : out) if (e.path == o.path) { e = o; merged = true; }
            if (!merged) out.push_back(o);
        }
    }
    if (out.empty()) say("Nothing to write.");
    return fine && !out.empty();
}

} // namespace nfsnl
