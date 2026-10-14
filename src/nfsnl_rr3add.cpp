// nfsnl_rr3add.cpp - Real Racing 3: adding a new car to the game
//
// What the game needs for one more car, all read out of 14.0.1's own files
// (every .nct is RC4, see nfsnl_rr3.cpp):
//
//   vehicles/<folder>/...                       the models (.m3g a..h, int_a/b,
//                                               shared), .points, .dashplanes,
//                                               textures, livery/ textures
//   vehicles/data/<id>_<folder>.bin.nct         the car's data (nfsnl_rr3car.cpp);
//                                               6 numbers before the price sits
//                                               the car's appearance index
//   vehicles/data/liveries/<folder>.liveries.bin.nct
//                                               texture slots and the liveries
//   vehicles/data/cardesc.list.nct              every car's data file name:
//                                               u32 length + "<id>_<folder>.bin",
//                                               sorted by id, no count
//   vehicles/data/carappearances.appearances.nct
//                                               u32 version (23), u32 count, then
//                                               per car: u32 appearance index,
//                                               the model folder name, then the
//                                               car's look (exhaust flames, lights...)
//   vehicles/data/cardamagethresholds.damage.nct
//                                               u32 count, 116 bytes per car,
//                                               starting with the car id
//   vehicles/data/suspensionbehaviour.suspensiondata.nct
//                                               u32 1, u32 count, 140 bytes per car,
//                                               starting with the car id
//   gametext.txt + text_<language>.txt          the name: one key per line in
//                                               gametext.txt, the same line in each
//                                               language file
//   asset_list_base.txtCache.txt                u32 0xA4F6F39D, u32 4, then
//                                               "<version>\n" and "path\tmd5\n" lines:
//                                               what the game believes is on disk
//
// The appearance index is how the data file finds its model folder; the
// folder name does not appear in the data file at all.
#include "nfsnl.h"
#include "nfsnl_save.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <filesystem>
#include <map>

namespace nfsnl {

namespace {

uint32_t rd(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put(Bytes& b, uint32_t v) { for (int k = 0; k < 4; ++k) b.push_back((uint8_t)(v >> (8 * k))); }
void putStr(Bytes& b, const std::string& s) { put(b, (uint32_t)s.size()); b.insert(b.end(), s.begin(), s.end()); }
void set32(Bytes& b, size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) b[at + k] = (uint8_t)(v >> (8 * k)); }

std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); return s; }
std::string upper(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A'); return s; }
std::string leafOf(const std::string& p) { size_t s = p.find_last_of("/\\"); return s == std::string::npos ? p : p.substr(s + 1); }
bool endsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }

// a reader over a decoded file; any overrun sets bad
struct Rd {
    const uint8_t* p; size_t n, i = 0; bool bad = false;
    Rd(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
    uint32_t u() { if (i + 4 > n) { bad = true; i = n; return 0; } uint32_t v = rd(p + i); i += 4; return v; }
    std::string s() {
        uint32_t L = u();
        if (bad || L > n - i) { bad = true; i = n; return std::string(); }
        std::string v((const char*)p + i, L); i += L; return v;
    }
    Bytes raw(size_t k) { if (i + k > n) { bad = true; i = n; return Bytes(); } Bytes v(p + i, p + i + k); i += k; return v; }
    uint8_t b() { if (i + 1 > n) { bad = true; i = n; return 0; } return p[i++]; }
};

} // namespace

// ============================================================ liveries

bool rr3LiveriesRead(const uint8_t* plain, size_t len, Rr3Liveries& L) {
    L = Rr3Liveries();
    Rd r(plain, len);
    L.car = r.s();
    uint32_t n = r.u();
    for (uint32_t k = 0; k < n && !r.bad && k < 100000; ++k) { uint32_t id = r.u(); L.textures.push_back({ id, r.s() }); }
    n = r.u();
    for (uint32_t k = 0; k < n && !r.bad && k < 100000; ++k) { uint32_t a = r.u(); L.materials.push_back({ a, r.u() }); }
    n = r.u();
    for (uint32_t k = 0; k < n && !r.bad && k < 100000; ++k) {
        std::array<uint32_t, 3> e;
        e[0] = r.u(); e[1] = r.u(); e[2] = r.u();
        L.extra.push_back(e);
    }
    n = r.u();
    for (uint32_t k = 0; k < n && !r.bad && k < 10000; ++k) {
        Rr3Livery v;
        v.file = r.s();
        v.display = r.s();
        v.a = r.raw(8);
        v.swatch = r.s();
        v.logo = r.s();
        v.flag = r.b();
        v.arms = r.s();
        v.driver = r.s();
        v.c = r.raw(4);
        Bytes rgb = r.raw(3);
        if (rgb.size() == 3) memcpy(v.rgb, rgb.data(), 3);
        uint32_t m = r.u();
        for (uint32_t q = 0; q < m && !r.bad && q < 100000; ++q) { uint32_t id = r.u(); v.overrides.push_back({ id, r.s() }); }
        m = r.u();
        for (uint32_t q = 0; q < m && !r.bad && q < 100000; ++q) { uint32_t a = r.u(); v.materials.push_back({ a, r.u() }); }
        L.liveries.push_back(v);
    }
    L.ok = !r.bad && r.i == len;
    return L.ok;
}

Bytes rr3LiveriesWrite(const Rr3Liveries& L) {
    Bytes b;
    putStr(b, L.car);
    put(b, (uint32_t)L.textures.size());
    for (const auto& t : L.textures) { put(b, t.first); putStr(b, t.second); }
    put(b, (uint32_t)L.materials.size());
    for (const auto& m : L.materials) { put(b, m.first); put(b, m.second); }
    put(b, (uint32_t)L.extra.size());
    for (const auto& e : L.extra) { put(b, e[0]); put(b, e[1]); put(b, e[2]); }
    put(b, (uint32_t)L.liveries.size());
    for (const Rr3Livery& v : L.liveries) {
        putStr(b, v.file);
        putStr(b, v.display);
        Bytes a = v.a; a.resize(8, 0);
        b.insert(b.end(), a.begin(), a.end());
        putStr(b, v.swatch);
        putStr(b, v.logo);
        b.push_back(v.flag);
        putStr(b, v.arms);
        putStr(b, v.driver);
        Bytes c = v.c; c.resize(4, 0);
        b.insert(b.end(), c.begin(), c.end());
        b.insert(b.end(), v.rgb, v.rgb + 3);
        put(b, (uint32_t)v.overrides.size());
        for (const auto& o : v.overrides) { put(b, o.first); putStr(b, o.second); }
        put(b, (uint32_t)v.materials.size());
        for (const auto& m : v.materials) { put(b, m.first); put(b, m.second); }
    }
    return b;
}

// ============================================================ helpers

// Every length-prefixed string in `b` that contains `from` gets it replaced by
// `to`, its length prefix fixed up. A string is recognised by its prefix: a
// u32 just before it that equals the length of a printable run holding `from`.
size_t rr3RenameStrings(Bytes& b, const std::string& from, const std::string& to) {
    if (from.empty() || b.size() < 8) return 0;
    struct Hit { size_t start, len; };
    std::vector<Hit> hits;
    size_t pos = 0;
    while (true) {
        auto it = std::search(b.begin() + pos, b.end(), from.begin(), from.end());
        if (it == b.end()) break;
        size_t at = (size_t)(it - b.begin());
        bool found = false;
        size_t lowest = at > 256 ? at - 256 : 4;
        if (lowest < 4) lowest = 4;
        for (size_t s = at + 1; s-- > lowest;) {
            if (s < 4) break;
            uint32_t L = rd(&b[s - 4]);
            if (L >= from.size() && L <= 4096 && s + L <= b.size() && s + L >= at + from.size()) {
                bool printable = true;
                for (size_t k = s; k < s + L && printable; ++k) printable = b[k] >= 32 && b[k] < 127;
                if (printable) { hits.push_back({ s, L }); found = true; pos = s + L; break; }
            }
        }
        if (!found) pos = at + 1;
        if (pos >= b.size()) break;
    }
    for (size_t h = hits.size(); h-- > 0;) {
        std::string v((const char*)&b[hits[h].start], hits[h].len);
        size_t k = 0;
        while ((k = v.find(from, k)) != std::string::npos) { v.replace(k, from.size(), to); k += to.size(); }
        b.erase(b.begin() + hits[h].start, b.begin() + hits[h].start + hits[h].len);
        b.insert(b.begin() + hits[h].start, v.begin(), v.end());
        set32(b, hits[h].start - 4, (uint32_t)v.size());
    }
    return hits.size();
}

namespace {

// the game's file at a path relative to the depot root, among `assets`
std::string findRel(const std::vector<std::string>& assets, const std::string& rel) {
    std::string want = lower(rel);
    for (char& c : want) if (c == '\\') c = '/';
    for (const std::string& a : assets) {
        std::string l = lower(a);
        for (char& c : l) if (c == '\\') c = '/';
        if (l == want || (l.size() > want.size() && endsWith(l, want) && l[l.size() - want.size() - 1] == '/'))
            return a;
    }
    return std::string();
}

// relative path of an asset from the depot root, judged by a known anchor
std::string relOf(const std::string& asset) {
    std::string l = asset;
    for (char& c : l) if (c == '\\') c = '/';
    std::string ll = lower(l);
    for (const char* anchor : { "vehicles/", "tracks/", "data/", "gui_assets/" }) {
        size_t at = ll.rfind(std::string("/") + anchor);
        if (at != std::string::npos) return l.substr(at + 1);
        if (ll.compare(0, strlen(anchor), anchor) == 0) return l;
    }
    return leafOf(l);
}

bool readNct(const std::vector<std::string>& assets, const std::function<bool(const std::string&, Bytes&)>& read,
             const std::string& rel, Bytes& plain, std::string& why) {
    std::string a = findRel(assets, rel);
    if (a.empty()) { why = rel + " is not among the files opened"; return false; }
    Bytes raw;
    if (!read(a, raw) || raw.empty()) { why = rel + " could not be read"; return false; }
    nctTransform(raw.data(), raw.size(), plain, nullptr);
    return true;
}

Bytes encNct(const Bytes& plain) { Bytes out; nctEncode(plain.data(), plain.size(), out); return out; }

std::string md5Hex(const Bytes& b) {
    uint8_t d[16];
    saves::md5(b.data(), b.size(), d);
    static const char* hx = "0123456789abcdef";
    std::string s;
    for (uint8_t c : d) { s += hx[c >> 4]; s += hx[c & 15]; }
    return s;
}

// cardesc.list: "<id>_<folder>.bin" names
bool readCarDesc(const Bytes& p, std::vector<std::string>& names) {
    names.clear();
    Rd r(p.data(), p.size());
    while (r.i < p.size() && !r.bad) names.push_back(r.s());
    return !r.bad && !names.empty();
}

uint32_t idOfDesc(const std::string& n) { return (uint32_t)strtoul(n.c_str(), nullptr, 10); }
std::string folderOfDesc(const std::string& n) {
    size_t us = n.find('_');
    std::string f = us == std::string::npos ? n : n.substr(us + 1);
    if (endsWith(f, ".bin")) f.resize(f.size() - 4);
    return f;
}

// carappearances: where each record starts (its index field) and its folder.
// The records' insides vary, so they are found by their start: u32 index,
// u32 length, a folder name of the form 1234_name. The count in the header
// has to agree or nothing is trusted.
struct Appearance { size_t start = 0, end = 0; uint32_t index = 0; std::string folder; };
bool readAppearances(const Bytes& p, std::vector<Appearance>& out, std::string& why) {
    out.clear();
    if (p.size() < 16) { why = "carappearances is too short"; return false; }
    uint32_t count = rd(&p[4]);
    for (size_t i = 8; i + 12 <= p.size(); ++i) {
        uint32_t L = rd(&p[i + 4]);
        if (L < 6 || L > 96 || i + 8 + L > p.size()) continue;
        const uint8_t* s = &p[i + 8];
        if (!(s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' &&
              s[3] >= '0' && s[3] <= '9' && s[4] == '_')) continue;
        bool ok = true;
        for (uint32_t k = 0; k < L && ok; ++k)
            ok = (s[k] >= 'a' && s[k] <= 'z') || (s[k] >= '0' && s[k] <= '9') || s[k] == '_' || s[k] == '-' ||
                 s[k] == '.' || s[k] == '(' || s[k] == ')';
        if (!ok) continue;
        if (rd(&p[i]) > 100000) continue;
        if (!out.empty() && i < out.back().start + 8 + out.back().folder.size()) continue;
        Appearance a;
        a.start = i;
        a.index = rd(&p[i]);
        a.folder.assign((const char*)s, L);
        out.push_back(a);
    }
    for (size_t k = 0; k < out.size(); ++k) out[k].end = k + 1 < out.size() ? out[k + 1].start : p.size();
    if (out.size() != count) {
        char b[160];
        snprintf(b, sizeof(b), "carappearances: found %zu car records, the header says %u", out.size(), count);
        why = b;
        return false;
    }
    return true;
}

// a text file's lines: gametext.txt holds keys, each language file the text
// on the same line. Kept as raw bytes, CRLF.
bool readTextFile(const Bytes& raw, Bytes& plain, bool& wasPlain) {
    wasPlain = rr3TextIsPlain(raw.data(), raw.size());
    if (wasPlain) plain = raw;
    else rr3TextTransform(raw.data(), raw.size(), plain);
    return !plain.empty();
}
Bytes writeTextFile(const Bytes& plain, bool asPlain) {
    if (asPlain) return plain;
    Bytes out;
    rr3TextTransform(plain.data(), plain.size(), out);
    return out;
}
// append "line\r\n" before the file's final empty line
void appendLine(Bytes& plain, const std::string& line) {
    if (plain.size() >= 2 && plain[plain.size() - 2] == '\r' && plain.back() == '\n') {
        plain.insert(plain.end(), line.begin(), line.end());
        plain.push_back('\r'); plain.push_back('\n');
    } else {
        plain.push_back('\r'); plain.push_back('\n');
        plain.insert(plain.end(), line.begin(), line.end());
        plain.push_back('\r'); plain.push_back('\n');
    }
}
bool hasLine(const Bytes& plain, const std::string& line) {
    std::string s(plain.begin(), plain.end());
    size_t at = 0;
    while ((at = s.find(line, at)) != std::string::npos) {
        bool startOk = at == 0 || s[at - 1] == '\n';
        bool endOk = at + line.size() == s.size() || s[at + line.size()] == '\r' || s[at + line.size()] == '\n';
        if (startOk && endOk) return true;
        at += line.size();
    }
    return false;
}

std::string slug(const std::string& s) {
    std::string o;
    for (char c : lower(s)) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c;
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o;
}

} // namespace

// ============================================================ asset list

bool rr3AssetListRead(const Bytes& raw, Rr3AssetList& L) {
    L = Rr3AssetList();
    if (raw.size() < 8 || rd(raw.data()) != 0xA4F6F39Du) return false;
    L.version = rd(&raw[4]);
    std::string s(raw.begin() + 8, raw.end());
    size_t i = 0;
    bool first = true;
    while (i < s.size()) {
        size_t e = s.find('\n', i);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(i, e - i);
        i = e + 1;
        if (first) { L.header = line; first = false; continue; }
        if (line.empty()) continue;
        size_t t = line.find('\t');
        if (t == std::string::npos) { L.entries.push_back({ line, std::string() }); continue; }
        L.entries.push_back({ line.substr(0, t), line.substr(t + 1) });
    }
    L.ok = true;
    return true;
}

Bytes rr3AssetListWrite(const Rr3AssetList& L) {
    Bytes b;
    put(b, 0xA4F6F39Du);
    put(b, L.version);
    std::string s = L.header + "\n";
    for (const auto& e : L.entries) s += e.first + (e.second.empty() ? "" : "\t" + e.second) + "\n";
    b.insert(b.end(), s.begin(), s.end());
    return b;
}

void rr3AssetListSet(Rr3AssetList& L, const std::string& rel, const std::string& md5, bool keepIfListed) {
    std::string p = "/" + rel;
    for (char& c : p) if (c == '\\') c = '/';
    for (auto& e : L.entries)
        if (e.first == p) { if (!keepIfListed) e.second = md5; return; }
    L.entries.push_back({ p, md5 });
}

// ============================================================ the car list

std::vector<Rr3CarInfo> rr3ListCars(const std::vector<std::string>& assets,
                                    const std::function<bool(const std::string&, Bytes&)>& read) {
    std::vector<Rr3CarInfo> out;
    Bytes desc;
    std::string why;
    std::vector<std::string> names;
    if (!readNct(assets, read, "vehicles/data/cardesc.list.nct", desc, why) || !readCarDesc(desc, names)) {
        // no list: every <id>_<folder>.bin.nct there is
        for (const std::string& a : assets) {
            std::string l = lower(relOf(a));
            if (l.compare(0, 14, "vehicles/data/") || !endsWith(l, ".bin.nct") || l.find('/', 14) != std::string::npos) continue;
            std::string leaf = leafOf(l);
            if (leaf[0] < '0' || leaf[0] > '9') continue;
            names.push_back(leaf.substr(0, leaf.size() - 4));
        }
    }
    std::vector<Appearance> apps;
    {
        Bytes ap;
        std::string w;
        if (readNct(assets, read, "vehicles/data/carappearances.appearances.nct", ap, w)) readAppearances(ap, apps, w);
    }
    for (const std::string& n : names) {
        Rr3CarInfo c;
        c.id = idOfDesc(n);
        c.folder = folderOfDesc(n);
        c.dataFile = n + ".nct";
        Bytes raw;
        std::string a = findRel(assets, "vehicles/data/" + c.dataFile);
        Rr3Car car;
        if (!a.empty() && read(a, raw) && rr3CarRead(raw.data(), raw.size(), car)) {
            for (const Rr3CarField& f : rr3CarFields(car)) {
                if (f.key == "make") c.make = f.value;
                else if (f.key == "model") c.model = f.value;
                else if (f.key == "year") c.year = f.value;
                else if (f.key == "make_key") c.makeKey = f.value;
                else if (f.key == "handling") c.profile = f.value;
                else if (f.key == "class") c.cls = f.value;
                else if (f.key == "drivetrain") c.drive = f.value;
                else if (f.key == "appearance") c.appearance = (uint32_t)strtoul(f.value.c_str(), nullptr, 10);
            }
            c.readable = true;
            for (const Appearance& a : apps) if (a.index == c.appearance) c.modelFolder = a.folder;
        }
        if (c.modelFolder.empty()) c.modelFolder = c.folder;
        out.push_back(c);
    }
    return out;
}

// ============================================================ adding a car

namespace {

// an OBJ / FBX built on the frame of one of the donor's .m3g files
bool buildModel(const std::string& src, const Bytes& tmpl, Bytes& out, std::string& note) {
    Bytes srcBytes;
    if (!readFile(src, srcBytes)) { note = "could not read " + leafOf(src); return false; }
    Model m = readImportModel(srcBytes.data(), srcBytes.size(), src);
    if (!m.valid) { note = leafOf(src) + " has no meshes"; return false; }
    std::vector<NameFix> fixes = rr3SuggestNames(tmpl, m, true);
    rr3ApplyNameFixes(m, fixes);
    std::string r;
    if (!rr3BuildCar(tmpl, m, out, r)) { note = r; return false; }
    note = r.substr(0, r.find('\n'));
    if (!fixes.empty()) note += " (" + std::to_string(fixes.size()) + " name(s) matched to the car's)";
    return true;
}

bool isTexture(const std::string& leaf) {
    std::string l = lower(leaf);
    return l.find(".etc.dds") != std::string::npos || l.find(".pvr") != std::string::npos ||
           l.find(".atc.dds") != std::string::npos || endsWith(l, ".dds");
}
std::string texStem(const std::string& leaf) { size_t d = leaf.find('.'); return lower(d == std::string::npos ? leaf : leaf.substr(0, d)); }

// a picture into one of the game's texture files, the .z wrapper kept
bool pictureInto(const std::string& leaf, const Bytes& orig, const Image& img, Bytes& out, std::string& note) {
    Bytes inner;
    std::string innerName;
    size_t frames = 0;
    bool wrapped = endsWith(lower(leaf), ".z");
    if (!(wrapped && rr3Unwrap(leaf, orig.data(), orig.size(), inner, &innerName, &frames) && !inner.empty()))
        { inner = orig; wrapped = false; }
    Image cur;
    if (!decodeTextureFile(inner.data(), inner.size(), cur)) { note = leaf + " could not be read"; return false; }
    Image use = toRgba(img);
    if (use.width != cur.width || use.height != cur.height) use = resizeRgba(use, cur.width, cur.height);
    Bytes enc;
    std::string r;
    if (!importPicture(wrapped ? (innerName.empty() ? leaf : innerName) : leaf, inner, use, enc, r)) { note = r; return false; }
    out = wrapped ? rr3WrapZ(enc) : enc;
    note = (img.width != cur.width || img.height != cur.height)
               ? "resized to " + std::to_string(cur.width) + "x" + std::to_string(cur.height) : "";
    return true;
}

// A livery's body texture repainted: only the paint changes colour - badges,
// lettering, carbon, lights and panel lines stay as they are. The paint is
// the texture's commonest colour. A coloured paint is found by its hue and
// each pixel moves by how much of the paint it holds (panel edges are part
// paint, part shadow); a white, silver or black paint has no hue, so there
// the pixels close to it in colour and brightness are repainted.
Image tint(const Image& src, const uint8_t rgb[3]) {
    Image o = toRgba(src);
    size_t n = (size_t)o.width * o.height;
    if (!n) return o;
    auto lumaOf = [](float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; };
    // the paint: the commonest 16-step colour, then the mean of what is near it
    std::vector<uint32_t> bins(4096, 0);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &o.pixels[i * 4];
        if (lumaOf(p[0], p[1], p[2]) <= 25) continue;
        ++bins[(p[0] >> 4) * 256 + (p[1] >> 4) * 16 + (p[2] >> 4)];
    }
    size_t k = std::max_element(bins.begin(), bins.end()) - bins.begin();
    float bb[3] = { (float)(k / 256) * 16 + 8, (float)((k / 16) % 16) * 16 + 8, (float)(k % 16) * 16 + 8 };
    double sum[3] = { 0, 0, 0 };
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &o.pixels[i * 4];
        float d = std::sqrt((p[0] - bb[0]) * (p[0] - bb[0]) + (p[1] - bb[1]) * (p[1] - bb[1]) + (p[2] - bb[2]) * (p[2] - bb[2]));
        if (d < 40) { sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; ++cnt; }
    }
    if (!cnt) return o;
    float base[3] = { (float)(sum[0] / cnt), (float)(sum[1] / cnt), (float)(sum[2] / cnt) };
    float lb = std::max(1.0f, lumaOf(base[0], base[1], base[2]));
    float bmax = std::max(base[0], std::max(base[1], base[2])), bmin = std::min(base[0], std::min(base[1], base[2]));
    float sb = (bmax - bmin) / std::max(bmax, 1.0f);
    auto hueOf = [](float r, float g, float b) {
        float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = std::max(mx - mn, 1e-3f), h;
        if (mx == r) h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        if (h < 0) h += 6;
        return h * 60.0f;
    };
    float hb = hueOf(base[0], base[1], base[2]);
    float t[3] = { (float)rgb[0], (float)rgb[1], (float)rgb[2] };
    for (size_t i = 0; i < n; ++i) {
        uint8_t* p = &o.pixels[i * 4];
        float c[3] = { (float)p[0], (float)p[1], (float)p[2] };
        float lp = lumaOf(c[0], c[1], c[2]) + 1e-3f, r = lp / lb;
        float out[3];
        if (sb > 0.2f) {
            float mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
            float sp = (mx - mn) / std::max(mx, 1.0f);
            float dh = std::fabs(hueOf(c[0], c[1], c[2]) - hb);
            dh = std::min(dh, 360 - dh);
            float hw = std::min(1.0f, std::max(0.0f, 1 - (dh - 15) / 25));
            float alpha = std::min(1.0f, sp / sb) * hw;
            float rr = std::min(r, 2.5f);
            for (int q = 0; q < 3; ++q) out[q] = c[q] + alpha * (t[q] - base[q]) * rr;
        } else {
            float dc = 0;
            for (int q = 0; q < 3; ++q) { float x = c[q] / lp - base[q] / lb; dc += x * x; }
            dc = std::sqrt(dc);
            float w = std::min(1.0f, std::max(0.0f, 1 - dc / 0.35f));
            const float lo = 0.55f, hi = 1.8f;
            w *= std::min(1.0f, std::max(0.0f, std::min((r - lo) / (lo * 0.5f), (hi - r) / (hi * 0.3f))));
            for (int q = 0; q < 3; ++q) out[q] = c[q] * (1 - w) + std::min(255.0f, t[q] * r) * w;
        }
        for (int q = 0; q < 3; ++q) p[q] = (uint8_t)std::min(255.0f, std::max(0.0f, out[q] + 0.5f));
    }
    return o;
}

// New liveries in a liveries list. Each starts from one of the list's own
// (`from`, else the first): its record copied with the new name and colour,
// its body texture from the user's picture or the starting one repainted.
// `donor` is the list as the files on disk name them (for finding the
// texture to start from); `L` the one being written, named after `folder`.
// append: added after the liveries already there; else they replace them.
bool makeLiveries(const std::vector<Rr3NewLivery>& specs, const Rr3Liveries& donor, Rr3Liveries& L,
                  const std::vector<std::string>& donorFiles,
                  const std::function<bool(const std::string&, Bytes&)>& read,
                  const std::string& folder, bool append, std::map<std::string, Bytes>& files,
                  std::set<std::string>& isNew, const std::function<void(const std::string&)>& say, std::string& err) {
    if (L.liveries.empty() || donor.liveries.size() != L.liveries.size()) { err = "the car has no liveries to start from"; return false; }
    std::vector<Rr3Livery> made;
    std::string firstTex;
    // numbers and texture names not used yet
    int n = 0;
    std::set<std::string> taken;
    if (append) {
        for (const Rr3Livery& v : L.liveries) n = std::max(n, atoi(v.file.c_str()));
        for (const std::string& a : donorFiles) taken.insert(texStem(leafOf(a)));
    }
    for (const Rr3NewLivery& nl : specs) {
        ++n;
        int fromIx = nl.from >= 0 && nl.from < (int)L.liveries.size() ? nl.from : 0;
        Rr3Livery v = L.liveries[fromIx];
        char num[16];
        snprintf(num, sizeof(num), "%02d", n);
        v.file = std::string(num) + "_" + folder + ".livery";
        v.display = nl.name;
        memcpy(v.rgb, nl.rgb, 3);
        std::string s = slug(nl.name.empty() ? std::string("livery_") + num : nl.name);
        std::string stem = folder + "_ext_" + s;
        for (int k = 2; taken.count(lower(stem)); ++k) stem = folder + "_ext_" + s + "_" + std::to_string(k);
        taken.insert(lower(stem));
        std::string texRel = "livery/" + stem;
        // its body texture: the starting livery's, from a picture or repainted
        std::string donorTexRel;
        for (const auto& o : donor.liveries[fromIx].overrides)
            if (o.second.compare(0, 7, "livery/") == 0) { donorTexRel = o.second; break; }
        for (auto& o : v.overrides)
            if (o.second.compare(0, 7, "livery/") == 0) o.second = texRel + ".ptc.pvr.z";
        if (firstTex.empty()) firstTex = texRel + ".ptc.pvr.z";
        // the list says .ptc.pvr.z; on the device the file is .etc.dds.z
        // (or .pvr / .atc.dds), so it is found by its stem
        std::string donorLeaf = leafOf(donorTexRel);
        size_t d = donorLeaf.find('.');
        std::string donorStem = lower(d == std::string::npos ? donorLeaf : donorLeaf.substr(0, d));
        std::string srcAsset;
        for (const std::string& a : donorFiles) {
            std::string l = lower(leafOf(a));
            if (!donorStem.empty() && l.compare(0, donorStem.size() + 1, donorStem + ".") == 0) { srcAsset = a; break; }
        }
        if (srcAsset.empty())
            for (const std::string& a : donorFiles)
                if (lower(relOf(a)).find("/livery/") != std::string::npos) { srcAsset = a; break; }
        if (srcAsset.empty()) { err = "the car has no livery texture to start from (open its vehicles/<car>/livery folder too)"; return false; }
        Bytes orig;
        read(srcAsset, orig);
        std::string ext = leafOf(srcAsset).substr(leafOf(srcAsset).find('.'));
        std::string outRel = "vehicles/" + folder + "/" + texRel + ext;
        Bytes enc;
        std::string note;
        Image img;
        Bytes pb;
        bool painted = false;
        if (!nl.picture.empty()) {
            if (readFile(nl.picture, pb) && decodeImageFile(pb.data(), pb.size(), img) &&
                pictureInto(leafOf(srcAsset), orig, img, enc, note)) painted = true;
            else say("Livery " + nl.name + ": " + leafOf(nl.picture) + " could not be used" + (note.empty() ? "" : ": " + note));
        }
        if (!painted && nl.repaint) {
            Bytes inner;
            std::string innerName;
            size_t frames = 0;
            Image cur;
            if (rr3Unwrap(leafOf(srcAsset), orig.data(), orig.size(), inner, &innerName, &frames) &&
                decodeTextureFile(inner.data(), inner.size(), cur) &&
                pictureInto(leafOf(srcAsset), orig, tint(cur, nl.rgb), enc, note)) painted = true;
        }
        files[outRel] = painted ? enc : orig;
        isNew.insert(outRel);
        char line[400];
        snprintf(line, sizeof(line), "Livery %d \"%s\": %s, colour %02X%02X%02X%s", n, nl.name.c_str(), leafOf(outRel).c_str(),
                 nl.rgb[0], nl.rgb[1], nl.rgb[2],
                 painted ? (nl.picture.empty() ? " (repainted)" : " (from your picture)") : " (copied as it is)");
        say(line);
        made.push_back(v);
    }
    if (append) {
        L.liveries.insert(L.liveries.end(), made.begin(), made.end());
    } else {
        L.liveries = made;
        for (auto& t : L.textures)
            if (t.second.compare(0, 7, "livery/") == 0) t.second = firstTex;
    }
    return true;
}

} // namespace

bool rr3AddLiveries(const std::string& carFolder, const std::vector<Rr3NewLivery>& specs,
                    const std::vector<std::string>& assets,
                    const std::function<bool(const std::string&, Bytes&)>& read,
                    std::vector<Rr3CarOutput>& out, std::string& report) {
    auto say = [&](const std::string& s) { report += s + "\n"; };
    std::string folder = lower(carFolder), why;
    Bytes plain;
    std::string rel = "vehicles/data/liveries/" + folder + ".liveries.bin.nct";
    if (!readNct(assets, read, rel, plain, why)) { say("Liveries: " + why); return false; }
    Rr3Liveries L;
    if (!rr3LiveriesRead(plain.data(), plain.size(), L)) { say("Liveries: " + folder + ".liveries.bin could not be read"); return false; }
    std::vector<std::string> carFiles;
    std::string from = "vehicles/" + folder + "/";
    for (const std::string& a : assets) if (lower(relOf(a)).compare(0, from.size(), from) == 0) carFiles.push_back(a);
    std::map<std::string, Bytes> files;
    std::set<std::string> isNew;
    std::string err;
    Rr3Liveries donor = L;
    if (!makeLiveries(specs, donor, L, carFiles, read, folder, true, files, isNew, say, err)) { say("Liveries: " + err); return false; }
    files[rel] = encNct(rr3LiveriesWrite(L));
    say("Liveries: the car has " + std::to_string(L.liveries.size()) + " now.");
    std::string listAsset = findRel(assets, "asset_list_base.txtCache.txt");
    Bytes raw;
    Rr3AssetList AL;
    if (!listAsset.empty() && read(listAsset, raw) && rr3AssetListRead(raw, AL)) {
        for (const auto& kv : files) rr3AssetListSet(AL, kv.first, md5Hex(kv.second), true);
        files["asset_list_base.txtCache.txt"] = rr3AssetListWrite(AL);
    }
    for (auto& kv : files) out.push_back({ kv.first, kv.second, isNew.count(kv.first) ? "new livery" : "liveries" });
    return true;
}

namespace {
} // namespace

bool rr3AddCar(const Rr3NewCarJob& job, const std::vector<std::string>& assets,
               const std::function<bool(const std::string&, Bytes&)>& read,
               std::vector<Rr3CarOutput>& out, std::string& report) {
    out.clear();
    report.clear();
    auto say = [&](const std::string& s) { report += s + "\n"; };
    auto fail = [&](const std::string& s) { say("Stopped: " + s); return false; };
    std::string donor = lower(job.donor), folder = lower(job.folder);
    std::string why;

    // ---- the new folder name: lower case, 1234_name, not taken
    if (folder.size() < 6 || folder[4] != '_') return fail("the new car's folder must look like 2024_make_model");
    for (char c : folder)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return fail("the new car's folder may hold only a-z, 0-9, _ and -");
    for (int k = 0; k < 4; ++k) if (folder[k] < '0' || folder[k] > '9') return fail("the new car's folder starts with the year");

    // ---- the registries
    Bytes desc, appear, damage, susp;
    std::vector<std::string> names;
    if (!readNct(assets, read, "vehicles/data/cardesc.list.nct", desc, why) || !readCarDesc(desc, names)) return fail(why.empty() ? "cardesc.list could not be read" : why);
    if (!readNct(assets, read, "vehicles/data/carappearances.appearances.nct", appear, why)) return fail(why);
    if (!readNct(assets, read, "vehicles/data/cardamagethresholds.damage.nct", damage, why)) return fail(why);
    if (!readNct(assets, read, "vehicles/data/suspensionbehaviour.suspensiondata.nct", susp, why)) return fail(why);
    std::vector<Appearance> apps;
    if (!readAppearances(appear, apps, why)) return fail(why);
    if (damage.size() < 4 || damage.size() != 4 + 116 * (size_t)rd(damage.data())) return fail("cardamagethresholds is not laid out as expected");
    if (susp.size() < 8 || susp.size() != 8 + 140 * (size_t)rd(&susp[4])) return fail("suspensionbehaviour is not laid out as expected");

    // ---- the donor
    std::string donorDesc;
    uint32_t maxId = 0;
    for (const std::string& n : names) {
        maxId = std::max(maxId, idOfDesc(n));
        if (folderOfDesc(n) == folder) return fail("a car with the folder " + folder + " is already in the game (" + n + ")");
    }
    Bytes donorRaw;
    Rr3Car car;
    {
        // the donor is named by its model folder; its data file by the
        // appearance index inside, which is the only reliable link
        for (const std::string& n : names) {
            Bytes raw;
            std::string a = findRel(assets, "vehicles/data/" + n + ".nct");
            if (a.empty() || !read(a, raw)) continue;
            Rr3Car c;
            if (!rr3CarRead(raw.data(), raw.size(), c)) continue;
            uint32_t ai = 0;
            for (const Rr3CarField& f : rr3CarFields(c)) if (f.key == "appearance") ai = (uint32_t)strtoul(f.value.c_str(), nullptr, 10);
            std::string af;
            for (const Appearance& ap : apps) if (ap.index == ai) af = ap.folder;
            if (af == donor || folderOfDesc(n) == donor) { donorDesc = n; donorRaw = raw; car = c; if (af == donor) break; }
        }
    }
    if (donorDesc.empty()) return fail("no car data for " + donor + " among the files opened");
    const Appearance* donorApp = nullptr;
    uint32_t donorAppIndex = 0;
    for (const Rr3CarField& f : rr3CarFields(car)) if (f.key == "appearance") donorAppIndex = (uint32_t)strtoul(f.value.c_str(), nullptr, 10);
    for (const Appearance& a : apps) if (a.index == donorAppIndex) donorApp = &a;
    if (!donorApp) return fail("the donor's appearance record was not found");
    std::string modelFolder = donorApp->folder;      // what the files are named after
    uint32_t donorId = idOfDesc(donorDesc);

    uint32_t newId = job.id ? job.id : maxId + 1;
    for (const std::string& n : names) if (idOfDesc(n) == newId) return fail("car id " + std::to_string(newId) + " is taken by " + n);
    uint32_t newApp = 0;
    for (const Appearance& a : apps) newApp = std::max(newApp, a.index + 1);
    char line[256];
    snprintf(line, sizeof(line), "New car %u_%s (appearance %u), copied from %s (car %u, folder %s).",
             newId, folder.c_str(), newApp, donorDesc.c_str(), donorId, modelFolder.c_str());
    say(line);

    std::map<std::string, Bytes> files;        // rel path -> new bytes
    std::set<std::string> isNew;               // rel paths that did not exist

    // ---- the donor's folder, renamed
    std::string from = "vehicles/" + modelFolder + "/";
    std::vector<std::string> donorFiles;
    for (const std::string& a : assets) {
        std::string r = relOf(a);
        if (lower(r).compare(0, from.size(), from) == 0) donorFiles.push_back(a);
    }
    if (donorFiles.empty()) return fail("none of " + from + " is among the files opened - open the game's vehicles folder too");
    bool newLiveries = !job.liveries.empty();
    for (const std::string& a : donorFiles) {
        std::string r = relOf(a);
        std::string sub = r.substr(from.size());
        bool inLivery = lower(sub).compare(0, 7, "livery/") == 0;
        if (inLivery && newLiveries) continue;        // made below
        std::string leaf = leafOf(sub), dir = sub.substr(0, sub.size() - leaf.size());
        std::string nl = leaf;
        if (lower(nl).compare(0, modelFolder.size(), modelFolder) == 0) nl = folder + nl.substr(modelFolder.size());
        Bytes b;
        if (!read(a, b)) return fail("could not read " + r);
        std::string nr = "vehicles/" + folder + "/" + dir + nl;
        files[nr] = b;
        isNew.insert(nr);
    }

    // ---- models
    auto modelInto = [&](const std::string& src, const std::string& leafSuffix) {
        if (src.empty()) return;
        std::string rel = "vehicles/" + folder + "/" + folder + leafSuffix;
        auto it = files.find(rel);
        if (it == files.end()) { say("Model: the donor has no " + modelFolder + leafSuffix + " - " + leafOf(src) + " not used."); return; }
        Bytes built;
        std::string note;
        if (!buildModel(src, it->second, built, note)) { say("Model: " + leafOf(src) + " into " + leafOf(rel) + " failed: " + note); return; }
        it->second = built;
        say("Model: " + leafOf(rel) + " from " + leafOf(src) + " - " + note);
    };
    modelInto(job.modelPath, "_a.m3g");
    if (!job.modelPath.empty() && job.allLods) {
        // the lower levels of detail from the same model, so the car looks
        // right at a distance too
        for (const char* lod : { "_b.m3g", "_c.m3g", "_d.m3g", "_e.m3g", "_f.m3g", "_g.m3g", "_h.m3g" }) {
            std::string rel = "vehicles/" + folder + "/" + folder + lod;
            auto it = files.find(rel);
            if (it == files.end()) continue;
            Bytes built;
            std::string note;
            if (buildModel(job.modelPath, it->second, built, note)) it->second = built;
            else it->second = files["vehicles/" + folder + "/" + folder + "_a.m3g"];
        }
        say("Model: the lower levels of detail (b..h) built from the same model.");
    }
    modelInto(job.interiorPath, "_int_a.m3g");

    // ---- textures: a picture named after one of the car's textures
    for (const std::string& pic : job.pictures) {
        std::string stem = texStem(leafOf(pic));
        for (const std::string& pre : { folder + "_", modelFolder + "_" })
            if (stem.compare(0, pre.size(), pre) == 0) stem = stem.substr(pre.size());
        Bytes pb;
        Image img;
        if (!readFile(pic, pb) || !decodeImageFile(pb.data(), pb.size(), img)) { say("Texture: " + leafOf(pic) + " could not be read."); continue; }
        int used = 0;
        for (auto& kv : files) {
            std::string leaf = leafOf(kv.first);
            if (!isTexture(leaf) || kv.first.find("shadows") != std::string::npos) continue;
            std::string ts = texStem(leaf);
            if (ts.compare(0, folder.size() + 1, folder + "_") == 0) ts = ts.substr(folder.size() + 1);
            if (ts != stem) continue;
            Bytes enc;
            std::string note;
            if (pictureInto(leaf, kv.second, img, enc, note)) { kv.second = enc; ++used; say("Texture: " + leaf + " from " + leafOf(pic) + (note.empty() ? "" : " (" + note + ")")); }
            else say("Texture: " + leaf + ": " + note);
        }
        if (!used) say("Texture: " + leafOf(pic) + " matches none of the car's textures (name it like misc.png, int.png, wheel.png...).");
    }

    // ---- liveries
    Bytes livPlain;
    if (!readNct(assets, read, "vehicles/data/liveries/" + modelFolder + ".liveries.bin.nct", livPlain, why)) return fail(why);
    Rr3Liveries Ldonor, L;
    if (!rr3LiveriesRead(livPlain.data(), livPlain.size(), Ldonor)) return fail(modelFolder + ".liveries.bin could not be read");
    {
        // every string naming the donor's folder now names the new one
        Bytes b = rr3LiveriesWrite(Ldonor);
        rr3RenameStrings(b, modelFolder, folder);
        if (!rr3LiveriesRead(b.data(), b.size(), L)) return fail("the renamed liveries list did not read back");
    }
    if (newLiveries) {
        std::string err;
        if (!makeLiveries(job.liveries, Ldonor, L, donorFiles, read, folder, false, files, isNew, say, err)) return fail(err);
    }
    files["vehicles/data/liveries/" + folder + ".liveries.bin.nct"] = encNct(rr3LiveriesWrite(L));
    isNew.insert("vehicles/data/liveries/" + folder + ".liveries.bin.nct");

    // ---- the car data
    {
        auto set = [&](const std::string& k, const std::string& v) {
            if (!rr3CarSet(car, k, v, why)) say("Data: " + k + ": " + why);
        };
        set("id", std::to_string(newId));
        set("appearance", std::to_string(newApp));
        std::string nameKey = job.nameKey.empty() ? "CAR_NAME_" + upper(slug(job.modelName.empty() ? folder : job.modelName)) + "_" + std::to_string(newId) : job.nameKey;
        set("name_key", nameKey);
        if (!job.makeKey.empty()) set("make_key", job.makeKey);
        if (!job.make.empty()) set("make", job.make);
        if (!job.modelName.empty()) set("model", job.modelName);
        set("year", folder.substr(0, 4));
        if (!job.upgrades.empty()) set("handling", job.upgrades);
        // the donor may be locked behind an event or a limited-time series;
        // the new car is sold at the dealer unless told otherwise
        set("unlock", job.unlock);
        for (const auto& kv : job.fields) {
            if (kv.first == "id" || kv.first == "appearance" || kv.first == "name_key") continue;
            set(kv.first, kv.second);
        }
        if (!job.soundFrom.empty()) {
            Rr3Car from;
            if (!rr3CarRead(job.soundFrom.data(), job.soundFrom.size(), from) || !rr3CarCopySound(car, from, why))
                say("Data: the engine sound could not be copied: " + (why.empty() ? from.why : why));
            else say("Data: engine sound from " + (from.toks.size() > 4 && from.toks[3].str ? from.toks[3].s + " " + from.toks[4].s : std::string("the other car")) + ".");
        }
        Bytes plain = rr3CarPlain(car);
        std::string rel = "vehicles/data/" + std::to_string(newId) + "_" + folder + ".bin.nct";
        files[rel] = encNct(plain);
        isNew.insert(rel);
        say("Data: " + leafOf(rel) + ", name key " + nameKey + ".");

        // ---- the name in every language
        std::string gtAsset = findRel(assets, "gametext.txt");
        if (gtAsset.empty()) say("Text: gametext.txt is not among the files opened - the car's name will show as its key.");
        else {
            Bytes raw, plainGt;
            bool asPlain = false;
            read(gtAsset, raw);
            readTextFile(raw, plainGt, asPlain);
            std::string key = "GAMETEXT_" + nameKey;
            if (hasLine(plainGt, key)) say("Text: " + key + " is already there.");
            else {
                appendLine(plainGt, key);
                files["gametext.txt"] = writeTextFile(plainGt, asPlain);
                int langs = 0;
                for (const std::string& a : assets) {
                    std::string leaf = lower(leafOf(a));
                    if (leaf.compare(0, 5, "text_") || !endsWith(leaf, ".txt")) continue;
                    if (relOf(a) != leafOf(a) && lower(relOf(a)).find('/') != std::string::npos) continue;
                    Bytes lr, lp;
                    bool lpPlain = false;
                    if (!read(a, lr) || !readTextFile(lr, lp, lpPlain)) continue;
                    appendLine(lp, job.modelName.empty() ? folder : job.modelName);
                    files[leafOf(a)] = writeTextFile(lp, lpPlain);
                    ++langs;
                }
                say("Text: " + key + " added to gametext.txt and " + std::to_string(langs) + " language file(s).");
            }
        }
    }

    // ---- the registries
    {
        std::string n = std::to_string(newId) + "_" + folder + ".bin";
        std::vector<std::string> nn = names;
        nn.push_back(n);
        std::sort(nn.begin(), nn.end(), [](const std::string& a, const std::string& b) { return idOfDesc(a) < idOfDesc(b); });
        Bytes b;
        for (const std::string& s : nn) putStr(b, s);
        files["vehicles/data/cardesc.list.nct"] = encNct(b);
    }
    {
        Bytes rec(appear.begin() + donorApp->start, appear.begin() + donorApp->end);
        set32(rec, 0, newApp);
        rr3RenameStrings(rec, modelFolder, folder);
        Bytes b = appear;
        b.insert(b.end(), rec.begin(), rec.end());
        set32(b, 4, rd(&b[4]) + 1);
        files["vehicles/data/carappearances.appearances.nct"] = encNct(b);
    }
    auto addRecord = [&](const Bytes& src, size_t head, size_t countAt, size_t size, const std::string& rel) {
        uint32_t n = rd(&src[countAt]);
        Bytes rec;
        size_t insertAt = head + (size_t)n * size;
        for (uint32_t k = 0; k < n; ++k) {
            size_t at = head + (size_t)k * size;
            uint32_t id = rd(&src[at]);
            if (id == donorId) rec.assign(src.begin() + at, src.begin() + at + size);
            if (id > newId && insertAt == head + (size_t)n * size) insertAt = at;
        }
        if (rec.empty()) { say("Data: the donor has no record in " + leafOf(rel) + " - the first car's used."); rec.assign(src.begin() + head, src.begin() + head + size); }
        set32(rec, 0, newId);
        Bytes b = src;
        b.insert(b.begin() + insertAt, rec.begin(), rec.end());
        set32(b, countAt, n + 1);
        files[rel] = encNct(b);
    };
    addRecord(damage, 4, 0, 116, "vehicles/data/cardamagethresholds.damage.nct");
    addRecord(susp, 8, 4, 140, "vehicles/data/suspensionbehaviour.suspensiondata.nct");
    say("Lists: cardesc.list, carappearances, cardamagethresholds and suspensionbehaviour each have the car added.");

    // ---- the asset list
    std::string listAsset = findRel(assets, "asset_list_base.txtCache.txt");
    if (listAsset.empty()) say("Asset list: asset_list_base.txtCache.txt is not among the files opened - not updated.");
    else {
        Bytes raw;
        Rr3AssetList AL;
        if (!read(listAsset, raw) || !rr3AssetListRead(raw, AL)) say("Asset list: could not be read - not updated.");
        else {
            int added = 0;
            for (const auto& kv : files) {
                if (kv.first.find('/') == std::string::npos && kv.first != "gametext.txt") continue;   // language files are not listed
                bool brandNew = isNew.count(kv.first) > 0;
                size_t before = AL.entries.size();
                rr3AssetListSet(AL, kv.first, md5Hex(kv.second), !brandNew && job.keepListedMd5);
                if (AL.entries.size() > before) ++added;
            }
            files["asset_list_base.txtCache.txt"] = rr3AssetListWrite(AL);
            say("Asset list: " + std::to_string(added) + " new file(s) listed" +
                (job.keepListedMd5 ? "; the files it already listed keep the official checksum, so the game does not fetch them again." : "."));
        }
    }

    for (auto& kv : files) out.push_back({ kv.first, kv.second, isNew.count(kv.first) ? "new" : "changed" });
    snprintf(line, sizeof(line), "%zu file(s): %zu new, %zu changed.", out.size(), isNew.size(), out.size() - isNew.size());
    say(line);
    return true;
}


// ============================================================ car mods

namespace {

// the n-th line of a text file (0-based, CR LF or LF), or nullopt-ish ""
bool lineAt(const Bytes& plain, size_t n, std::string& out) {
    size_t line = 0, i = 0;
    while (i <= plain.size()) {
        size_t e = i;
        while (e < plain.size() && plain[e] != '\n') ++e;
        if (line == n) {
            size_t end = e;
            if (end > i && plain[end - 1] == '\r') --end;
            out.assign(plain.begin() + i, plain.begin() + end);
            return e < plain.size() || end > i;
        }
        ++line;
        i = e + 1;
        if (e >= plain.size()) break;
    }
    return false;
}
// the line number holding exactly `text`, or -1
long lineOf(const Bytes& plain, const std::string& text) {
    size_t line = 0, i = 0;
    while (i < plain.size()) {
        size_t e = i;
        while (e < plain.size() && plain[e] != '\n') ++e;
        size_t end = e;
        if (end > i && plain[end - 1] == '\r') --end;
        if (end - i == text.size() && std::equal(text.begin(), text.end(), plain.begin() + i)) return (long)line;
        ++line;
        i = e + 1;
    }
    return -1;
}

bool isRegistry(const std::string& rel) {
    std::string l = lower(rel), leaf = leafOf(l);
    return l == "vehicles/data/cardesc.list.nct" || l == "vehicles/data/carappearances.appearances.nct" ||
           l == "vehicles/data/cardamagethresholds.damage.nct" ||
           l == "vehicles/data/suspensionbehaviour.suspensiondata.nct" || leaf == "gametext.txt" ||
           leaf == "asset_list_base.txtcache.txt" || (leaf.compare(0, 5, "text_") == 0 && endsWith(leaf, ".txt"));
}

// a mod car's data file: vehicles/data/<id>_<folder>.bin.nct
bool modCarData(const std::string& rel, uint32_t& id, std::string& folder) {
    std::string l = lower(rel);
    if (l.compare(0, 14, "vehicles/data/") != 0 || !endsWith(l, ".bin.nct")) return false;
    std::string leaf = l.substr(14);
    if (leaf.find('/') != std::string::npos) return false;
    size_t us = leaf.find('_');
    if (us == std::string::npos || us == 0 || us > 6) return false;
    for (size_t k = 0; k < us; ++k) if (leaf[k] < '0' || leaf[k] > '9') return false;
    id = (uint32_t)strtoul(leaf.c_str(), nullptr, 10);
    folder = leaf.substr(us + 1, leaf.size() - us - 1 - 8);
    return id > 0 && folder.size() > 5;
}

} // namespace

std::vector<std::pair<std::string, std::string>> rr3ModScanFolder(const std::string& folder) {
    std::vector<std::pair<std::string, std::string>> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(std::filesystem::u8path(folder), ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string disk = it->path().u8string();
        std::string l = lower(leafOf(disk));
        if (endsWith(l, ".bak") || l == "thumbs.db" || l == ".ds_store") continue;
        std::string rel = relOf(disk);
        for (char& c : rel) if (c == '\\') c = '/';
        out.push_back({ rel, disk });
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool rr3ModRead(const std::vector<std::pair<std::string, std::string>>& files, Rr3Mod& mod,
                const std::vector<std::string>& assets,
                const std::function<bool(const std::string&, Bytes&)>& read, std::string& why) {
    mod = Rr3Mod();
    mod.files = files;
    Bytes desc;
    std::vector<std::string> names;
    if (!readNct(assets, read, "vehicles/data/cardesc.list.nct", desc, why) || !readCarDesc(desc, names)) {
        if (why.empty()) why = "the game's cardesc.list could not be read";
        return false;
    }
    std::map<uint32_t, std::string> bySlot;
    std::map<std::string, uint32_t> byFolder;
    for (const std::string& n : names) { bySlot[idOfDesc(n)] = n; byFolder[lower(folderOfDesc(n))] = idOfDesc(n); }

    std::set<uint32_t> planned;
    for (const auto& f : files) {
        Rr3ModCar c;
        if (!modCarData(f.first, c.id, c.folder)) continue;
        c.dataRel = f.first;
        Bytes raw, plain;
        Rr3Car car;
        if (readFile(f.second, raw) && nctTransform(raw.data(), raw.size(), plain, nullptr) &&
            rr3CarRead(raw.data(), raw.size(), car)) {
            std::string y, mk, md;
            for (const Rr3CarField& fl : rr3CarFields(car)) {
                if (fl.key == "year") y = fl.value;
                if (fl.key == "make") mk = fl.value;
                if (fl.key == "model") md = fl.value;
            }
            c.name = y + " " + mk + " " + md;
        } else {
            mod.replaces.push_back(f.first + " (not a car data file the tool can read - left out)");
            continue;
        }
        auto own = byFolder.find(lower(c.folder));
        if (own != byFolder.end()) {
            c.update = true;
            c.installAs = own->second;           // the game's car: its slot stays
        } else {
            auto t = bySlot.find(c.id);
            if (t != bySlot.end() || planned.count(c.id)) {
                c.slotTakenBy = t != bySlot.end() ? t->second : std::string("another car of this mod");
                if (t != bySlot.end()) {
                    // that car's shown name, from its data
                    Bytes r2;
                    Rr3Car other;
                    std::string a = findRel(assets, "vehicles/data/" + t->second + ".nct");
                    if (!a.empty() && read(a, r2) && rr3CarRead(r2.data(), r2.size(), other)) {
                        std::string y, mk, md;
                        for (const Rr3CarField& fl : rr3CarFields(other)) {
                            if (fl.key == "year") y = fl.value;
                            if (fl.key == "make") mk = fl.value;
                            if (fl.key == "model") md = fl.value;
                        }
                        c.takenByName = y + " " + mk + " " + md;
                    }
                }
                uint32_t free = c.id + 1;
                while (bySlot.count(free) || planned.count(free)) ++free;
                c.installAs = free;               // offered; the caller asks
            } else {
                c.installAs = c.id;
            }
        }
        planned.insert(c.installAs);
        mod.cars.push_back(c);
    }
    if (mod.cars.empty()) {
        why = "no car in this folder: a car mod has vehicles/data/<id>_<car>.bin.nct and vehicles/<car>/";
        return false;
    }
    // the mod's other files that are the game's own: listed, copied only when asked
    for (const auto& f : files) {
        if (isRegistry(f.first)) continue;
        std::string l = lower(f.first);
        bool ofCar = false;
        for (const Rr3ModCar& c : mod.cars) {
            std::string folder = lower(c.folder);
            if (l.compare(0, 9 + folder.size() + 1, "vehicles/" + folder + "/") == 0 ||
                l == "vehicles/data/liveries/" + folder + ".liveries.bin.nct" || l == lower(c.dataRel))
                ofCar = true;
        }
        if (ofCar) continue;
        if (!findRel(assets, f.first).empty()) mod.replaces.push_back(f.first);
    }
    return true;
}

bool rr3ModInstall(const Rr3Mod& mod, bool replaceGameFiles, const std::vector<std::string>& assets,
                   const std::function<bool(const std::string&, Bytes&)>& read,
                   std::vector<Rr3CarOutput>& out, std::string& report) {
    out.clear();
    report.clear();
    auto say = [&](const std::string& s) { report += s + "\n"; };
    auto fail = [&](const std::string& s) { say("Stopped: " + s); return false; };
    std::string why;
    auto modFile = [&](const std::string& rel, Bytes& b) {
        std::string want = lower(rel);
        for (const auto& f : mod.files) if (lower(f.first) == want) return readFile(f.second, b);
        return false;
    };
    auto modNct = [&](const std::string& rel, Bytes& plain) {
        Bytes raw;
        if (!modFile(rel, raw)) return false;
        return nctTransform(raw.data(), raw.size(), plain, nullptr);
    };

    // ---- the game's lists, as they are now
    Bytes desc, appear, damage, susp;
    std::vector<std::string> names;
    if (!readNct(assets, read, "vehicles/data/cardesc.list.nct", desc, why) || !readCarDesc(desc, names)) return fail(why.empty() ? "cardesc.list could not be read" : why);
    if (!readNct(assets, read, "vehicles/data/carappearances.appearances.nct", appear, why)) return fail(why);
    if (!readNct(assets, read, "vehicles/data/cardamagethresholds.damage.nct", damage, why)) return fail(why);
    if (!readNct(assets, read, "vehicles/data/suspensionbehaviour.suspensiondata.nct", susp, why)) return fail(why);
    std::vector<Appearance> apps;
    if (!readAppearances(appear, apps, why)) return fail(why);
    if (damage.size() < 4 || damage.size() != 4 + 116 * (size_t)rd(damage.data())) return fail("cardamagethresholds is not laid out as expected");
    if (susp.size() < 8 || susp.size() != 8 + 140 * (size_t)rd(&susp[4])) return fail("suspensionbehaviour is not laid out as expected");
    // the mod's own lists: only to take its cars' records from
    Bytes mAppear, mDamage, mSusp;
    std::vector<Appearance> mApps;
    bool haveMApp = modNct("vehicles/data/carappearances.appearances.nct", mAppear) && readAppearances(mAppear, mApps, why);
    bool haveMDam = modNct("vehicles/data/cardamagethresholds.damage.nct", mDamage) && mDamage.size() >= 4 &&
                    mDamage.size() == 4 + 116 * (size_t)rd(mDamage.data());
    bool haveMSus = modNct("vehicles/data/suspensionbehaviour.suspensiondata.nct", mSusp) && mSusp.size() >= 8 &&
                    mSusp.size() == 8 + 140 * (size_t)rd(&mSusp[4]);
    // texts: the game's and the mod's
    Bytes gt, mgt;
    bool gtPlain = false, mgtPlain = false;
    std::string gtAsset = findRel(assets, "gametext.txt");
    bool haveGt = !gtAsset.empty() && [&] { Bytes r; return read(gtAsset, r) && readTextFile(r, gt, gtPlain); }();
    bool haveMgt = [&] { Bytes r; return modFile("gametext.txt", r) && readTextFile(r, mgt, mgtPlain); }();
    struct Lang { std::string asset, leaf; Bytes plain; bool asPlain = false; bool changed = false; };
    std::vector<Lang> langs;
    for (const std::string& a : assets) {
        std::string leaf = lower(leafOf(a));
        if (leaf.compare(0, 5, "text_") || !endsWith(leaf, ".txt")) continue;
        if (relOf(a) != leafOf(a) && lower(relOf(a)).find('/') != std::string::npos) continue;
        Lang L;
        L.asset = a;
        L.leaf = leafOf(a);
        Bytes r;
        if (read(a, r) && readTextFile(r, L.plain, L.asPlain)) langs.push_back(L);
    }
    bool gtChanged = false;
    // a key's text in every language: the mod's line, or its English, or the plain fallback
    auto addText = [&](const std::string& key, const std::string& fallback) {
        if (!haveGt) return;
        std::string full = "GAMETEXT_" + key;
        if (hasLine(gt, full)) return;
        long at = haveMgt ? lineOf(mgt, full) : -1;
        appendLine(gt, full);
        gtChanged = true;
        std::string english;
        if (at >= 0) {
            Bytes r, p;
            bool pp;
            if (modFile("text_en.txt", r) && readTextFile(r, p, pp)) lineAt(p, (size_t)at, english);
        }
        for (Lang& L : langs) {
            std::string t;
            Bytes r, p;
            bool pp;
            if (at >= 0 && modFile(L.leaf, r) && readTextFile(r, p, pp)) lineAt(p, (size_t)at, t);
            if (t.empty()) t = english.empty() ? fallback : english;
            appendLine(L.plain, t);
            L.changed = true;
        }
    };

    std::map<std::string, Bytes> files;
    std::set<std::string> isNew;
    auto put = [&](const std::string& rel, const Bytes& b) {
        files[rel] = b;
        if (findRel(assets, rel).empty()) isNew.insert(rel);
    };
    uint32_t nextApp = 0;
    for (const Appearance& a : apps) nextApp = std::max(nextApp, a.index + 1);

    for (const Rr3ModCar& c : mod.cars) {
        std::string folder = lower(c.folder);
        Bytes raw;
        Rr3Car car;
        if (!modFile(c.dataRel, raw) || !rr3CarRead(raw.data(), raw.size(), car)) { say(c.dataRel + ": could not be read - car left out."); continue; }
        uint32_t modApp = 0;
        std::string nameKey, makeKey, model;
        for (const Rr3CarField& f : rr3CarFields(car)) {
            if (f.key == "appearance") modApp = (uint32_t)strtoul(f.value.c_str(), nullptr, 10);
            if (f.key == "name_key") nameKey = f.value;
            if (f.key == "make_key") makeKey = f.value;
            if (f.key == "model") model = f.value;
        }
        // ---- the appearance record: the game's for this folder, else the mod's, renumbered
        const Appearance* gameApp = nullptr;
        for (const Appearance& a : apps) if (lower(a.folder) == folder) gameApp = &a;
        const Appearance* modAppRec = nullptr;
        if (haveMApp) for (const Appearance& a : mApps) if (a.index == modApp && lower(a.folder) == folder) modAppRec = &a;
        if (haveMApp && !modAppRec) for (const Appearance& a : mApps) if (lower(a.folder) == folder) modAppRec = &a;
        uint32_t useApp;
        if (gameApp) {
            useApp = gameApp->index;
            if (modAppRec) {
                // the mod's record replaces the game's, in place
                Bytes rec(mAppear.begin() + modAppRec->start, mAppear.begin() + modAppRec->end);
                set32(rec, 0, useApp);
                appear.erase(appear.begin() + gameApp->start, appear.begin() + gameApp->end);
                appear.insert(appear.begin() + gameApp->start, rec.begin(), rec.end());
                if (!readAppearances(appear, apps, why)) return fail("carappearances: " + why);
            }
        } else if (modAppRec) {
            useApp = nextApp++;
            Bytes rec(mAppear.begin() + modAppRec->start, mAppear.begin() + modAppRec->end);
            set32(rec, 0, useApp);
            appear.insert(appear.end(), rec.begin(), rec.end());
            set32(appear, 4, rd(&appear[4]) + 1);
            if (!readAppearances(appear, apps, why)) return fail("carappearances: " + why);
        } else {
            say(c.folder + ": the mod has no appearance record for it (vehicles/data/carappearances) - car left out.");
            continue;
        }
        // ---- the car data, at its slot
        uint32_t slot = c.installAs ? c.installAs : c.id;
        rr3CarSet(car, "id", std::to_string(slot), why);
        rr3CarSet(car, "appearance", std::to_string(useApp), why);
        std::string oldTail = "_" + std::to_string(c.id);
        if (slot != c.id && nameKey.size() > oldTail.size() && endsWith(nameKey, oldTail)) {
            // CAR_NAME_<MODEL>_611 -> _612: the other car's key stays its own
            std::string nk = nameKey.substr(0, nameKey.size() - oldTail.size()) + "_" + std::to_string(slot);
            // its text comes from the mod under the old key
            if (haveGt && !hasLine(gt, "GAMETEXT_" + nk)) {
                long at = haveMgt ? lineOf(mgt, "GAMETEXT_" + nameKey) : -1;
                if (at >= 0) {
                    // rename the mod's line so addText finds it under the new key
                    std::string s(mgt.begin(), mgt.end());
                    std::string from = "GAMETEXT_" + nameKey;
                    size_t pos = s.find(from);
                    if (pos != std::string::npos) s.replace(pos, from.size(), "GAMETEXT_" + nk);
                    mgt.assign(s.begin(), s.end());
                }
            }
            rr3CarSet(car, "name_key", nk, why);
            nameKey = nk;
        }
        if (!nameKey.empty()) addText(nameKey, model.empty() ? c.folder : model);
        if (!makeKey.empty()) {
            std::string mk;
            for (const Rr3CarField& f : rr3CarFields(car)) if (f.key == "make") mk = f.value;
            addText(makeKey, mk);
        }
        std::string dataRel = "vehicles/data/" + std::to_string(slot) + "_" + c.folder + ".bin.nct";
        put(dataRel, encNct(rr3CarPlain(car)));

        // ---- cardesc.list: the car at its slot
        std::string entry = std::to_string(slot) + "_" + c.folder + ".bin";
        bool listed = false;
        for (const std::string& n : names) if (n == entry) listed = true;
        if (!listed) {
            // an update moved to another slot would leave its old line: drop it
            names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& n) {
                return lower(folderOfDesc(n)) == folder; }), names.end());
            names.push_back(entry);
        }
        // ---- damage and suspension: the mod's record for its car, at the slot
        auto setRecord = [&](Bytes& game, size_t head, size_t countAt, size_t size, const Bytes& modList, bool haveMod,
                             size_t modHead, size_t modCountAt, const char* what) {
            Bytes rec;
            if (haveMod) {
                uint32_t n = rd(&modList[modCountAt]);
                for (uint32_t k = 0; k < n; ++k)
                    if (rd(&modList[modHead + (size_t)k * size]) == c.id)
                        rec.assign(modList.begin() + modHead + (size_t)k * size, modList.begin() + modHead + (size_t)(k + 1) * size);
            }
            uint32_t n = rd(&game[countAt]);
            size_t existing = SIZE_MAX, insertAt = head + (size_t)n * size;
            for (uint32_t k = 0; k < n; ++k) {
                size_t at = head + (size_t)k * size;
                uint32_t id = rd(&game[at]);
                if (id == slot) existing = at;
                if (id > slot && insertAt == head + (size_t)n * size) insertAt = at;
            }
            if (rec.empty()) {
                if (existing != SIZE_MAX) return;            // the game's own stays
                say(c.folder + ": the mod has no " + what + " record - the game's first car's used.");
                rec.assign(game.begin() + head, game.begin() + head + size);
            }
            set32(rec, 0, slot);
            if (existing != SIZE_MAX) std::copy(rec.begin(), rec.end(), game.begin() + existing);
            else {
                game.insert(game.begin() + insertAt, rec.begin(), rec.end());
                set32(game, countAt, n + 1);
            }
        };
        setRecord(damage, 4, 0, 116, mDamage, haveMDam, 4, 0, "damage");
        setRecord(susp, 8, 4, 140, mSusp, haveMSus, 8, 4, "suspension");

        // ---- the car's own files
        int copied = 0;
        for (const auto& f : mod.files) {
            std::string l = lower(f.first);
            bool own = l.compare(0, 9 + folder.size() + 1, "vehicles/" + folder + "/") == 0 ||
                       l == "vehicles/data/liveries/" + folder + ".liveries.bin.nct";
            if (!own) continue;
            Bytes b;
            if (!readFile(f.second, b)) { say(f.first + ": could not be read."); continue; }
            put(f.first, b);
            ++copied;
        }
        char line[400];
        snprintf(line, sizeof(line), "%s (%s): %s slot %u%s, appearance %u, %d file(s).", c.name.c_str(), c.folder.c_str(),
                 c.update ? "updated in" : "installed in", slot,
                 slot != c.id ? (" - made for " + std::to_string(c.id) + ", which " + c.slotTakenBy + " has").c_str() : "",
                 useApp, copied);
        say(line);
    }

    // ---- the other files
    int other = 0, skipped = 0;
    for (const auto& f : mod.files) {
        if (files.count(f.first) || isRegistry(f.first)) continue;
        uint32_t id;
        std::string fo;
        if (modCarData(f.first, id, fo)) continue;          // written above, at its slot
        bool game = !findRel(assets, f.first).empty();
        if (game && !replaceGameFiles) { ++skipped; continue; }
        Bytes b;
        if (!readFile(f.second, b)) continue;
        put(f.first, b);
        ++other;
    }
    if (other) say(std::to_string(other) + " other file(s) of the mod copied.");
    if (skipped) say(std::to_string(skipped) + " file(s) of the mod that would replace the game's own were left out.");

    // ---- the lists
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return idOfDesc(a) < idOfDesc(b); });
    {
        Bytes b;
        for (const std::string& s : names) putStr(b, s);
        files["vehicles/data/cardesc.list.nct"] = encNct(b);
    }
    files["vehicles/data/carappearances.appearances.nct"] = encNct(appear);
    files["vehicles/data/cardamagethresholds.damage.nct"] = encNct(damage);
    files["vehicles/data/suspensionbehaviour.suspensiondata.nct"] = encNct(susp);
    if (gtChanged) {
        files["gametext.txt"] = writeTextFile(gt, gtPlain);
        for (const Lang& L : langs) if (L.changed) files[L.leaf] = writeTextFile(L.plain, L.asPlain);
    }
    say("Lists: cardesc.list, carappearances, cardamagethresholds and suspensionbehaviour as the game has them, with the mod's car(s) in" +
        std::string(gtChanged ? "; the names added to gametext.txt and the language files." : "."));

    // ---- the asset list
    std::string listAsset = findRel(assets, "asset_list_base.txtCache.txt");
    if (!listAsset.empty()) {
        Bytes raw;
        Rr3AssetList AL;
        if (read(listAsset, raw) && rr3AssetListRead(raw, AL)) {
            int added = 0;
            for (const auto& kv : files) {
                if (kv.first.find('/') == std::string::npos && kv.first != "gametext.txt") continue;
                size_t before = AL.entries.size();
                rr3AssetListSet(AL, kv.first, md5Hex(kv.second), !isNew.count(kv.first));
                if (AL.entries.size() > before) ++added;
            }
            files["asset_list_base.txtCache.txt"] = rr3AssetListWrite(AL);
            say("Asset list: " + std::to_string(added) + " new file(s) listed.");
        }
    }
    for (auto& kv : files) out.push_back({ kv.first, kv.second, isNew.count(kv.first) ? "new" : "changed" });
    return true;
}

} // namespace nfsnl
