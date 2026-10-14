// glTF 2.0 binary (.glb) export.
//
// The same scene the FBX export writes - the chosen body kit, one node per
// LOD, one per part under it, the hardpoints as empties - in one
// self-contained file: geometry, materials and the textures as PNG inside it.
// No Limits' HELPERS (collision boxes, the decal shell) are left out: glTF has
// no hidden flag that viewers honour, and drawn they wrap the car.
#include "nfsnl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace nfsnl {

namespace {

const char* kVersion = "Monkey Tool 1.3.0";

std::string jsonString(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c < 0x20 || c >= 0x80) {
            // control characters and anything past ASCII (the games' names
            // are ASCII; a stray Latin-1 byte becomes its code point)
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o += (char)c;
    }
    return o + "\"";
}

std::string num(double v) {
    if (!std::isfinite(v)) v = 0;
    char b[32];
    snprintf(b, sizeof(b), "%.7g", v);
    return b;
}

struct Bin {
    Bytes data;
    std::vector<std::string> views;       // JSON of each bufferView
    std::vector<std::string> accessors;   // JSON of each accessor
    // append bytes 4-aligned as a bufferView; its index
    int view(const void* p, size_t n, int target) {
        while (data.size() % 4) data.push_back(0);
        size_t off = data.size();
        data.insert(data.end(), (const uint8_t*)p, (const uint8_t*)p + n);
        std::string v = "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) +
                        ",\"byteLength\":" + std::to_string(n);
        if (target) v += ",\"target\":" + std::to_string(target);
        v += "}";
        views.push_back(v);
        return (int)views.size() - 1;
    }
    // float vectors of `comps` (2, 3, 4); with min/max for positions
    int floats(const std::vector<float>& v, int comps, bool bounds) {
        size_t count = v.size() / comps;
        int bv = view(v.data(), count * comps * 4, 34962);
        static const char* kType[] = { "", "SCALAR", "VEC2", "VEC3", "VEC4" };
        std::string a = "{\"bufferView\":" + std::to_string(bv) + ",\"componentType\":5126,\"count\":" +
                        std::to_string(count) + ",\"type\":\"" + kType[comps] + "\"";
        if (bounds && count) {
            float lo[4], hi[4];
            for (int c = 0; c < comps; ++c) lo[c] = hi[c] = v[c];
            for (size_t i = 0; i < count; ++i)
                for (int c = 0; c < comps; ++c) {
                    lo[c] = std::min(lo[c], v[i * comps + c]);
                    hi[c] = std::max(hi[c], v[i * comps + c]);
                }
            a += ",\"min\":[";
            for (int c = 0; c < comps; ++c) a += (c ? "," : "") + num(lo[c]);
            a += "],\"max\":[";
            for (int c = 0; c < comps; ++c) a += (c ? "," : "") + num(hi[c]);
            a += "]";
        }
        a += "}";
        accessors.push_back(a);
        return (int)accessors.size() - 1;
    }
    int indices(const std::vector<uint32_t>& idx) {
        int bv = view(idx.data(), idx.size() * 4, 34963);
        accessors.push_back("{\"bufferView\":" + std::to_string(bv) + ",\"componentType\":5125,\"count\":" +
                            std::to_string(idx.size()) + ",\"type\":\"SCALAR\"}");
        return (int)accessors.size() - 1;
    }
};

// the texture's file name the FBX export gives it ("<name>.png", no .z)
std::string pngName(const std::string& rel) {
    std::string bare = extensionOf(rel) == "z" ? stripExtension(rel) : rel;
    return baseName(stripExtension(bare) + ".png");
}

} // namespace

bool writeGlb(const Model& src, Bytes& out, const std::function<Bytes(const std::string&)>& pngFor,
              std::string* note, const std::string& sceneName) {
    out.clear();
    // the body kit the export asked for, as the FBX does; HELPERS left out
    std::vector<uint8_t> keep = kitMask(src, src.exportKit.empty() ? std::string("all") : src.exportKit);
    keep.resize(src.meshes.size(), 1);
    std::vector<const Mesh*> meshes;
    int helpers = 0;
    for (size_t i = 0; i < src.meshes.size(); ++i) {
        const Mesh& me = src.meshes[i];
        if (!keep[i]) continue;
        if (me.lod == "HELPERS") { ++helpers; continue; }
        if (me.positions.size() < 9 || me.indices.size() < 3) continue;
        meshes.push_back(&me);
    }
    if (meshes.empty() && src.points.empty()) return false;

    Bin bin;
    // ---- materials and their textures, by name ----
    std::map<std::string, const Material*> matByName;
    for (const Material& mt : src.materials) matByName[mt.name] = &mt;
    std::map<std::string, int> materialIndex;      // material (or "#tex:<path>") -> glTF material
    std::vector<std::string> materialsJson, texturesJson, imagesJson;
    std::map<std::string, int> textureIndex;       // texture path -> glTF texture
    int embedded = 0, linked = 0;
    auto textureFor = [&](const std::string& path) -> int {
        if (path.empty()) return -1;
        auto it = textureIndex.find(path);
        if (it != textureIndex.end()) return it->second;
        Bytes png = pngFor ? pngFor(path) : Bytes();
        std::string img;
        if (!png.empty()) {
            int bv = bin.view(png.data(), png.size(), 0);
            img = "{\"bufferView\":" + std::to_string(bv) + ",\"mimeType\":\"image/png\",\"name\":" +
                  jsonString(pngName(path)) + "}";
            ++embedded;
        } else {
            // not decodable here: point at the PNG the texture export writes
            img = "{\"uri\":" + jsonString(pngName(path)) + "}";
            ++linked;
        }
        imagesJson.push_back(img);
        texturesJson.push_back("{\"sampler\":0,\"source\":" + std::to_string(imagesJson.size() - 1) + "}");
        int t = (int)texturesJson.size() - 1;
        textureIndex[path] = t;
        return t;
    };
    auto materialFor = [&](const Mesh& me) -> int {
        const Material* mt = nullptr;
        auto mit = matByName.find(me.material);
        if (mit != matByName.end()) mt = mit->second;
        std::string tex = me.texture;
        if (mt && !mt->diffuse.empty()) tex = mt->diffuse;
        std::string key = me.material + "\x1f" + tex;
        auto it = materialIndex.find(key);
        if (it != materialIndex.end()) return it->second;
        std::string low = me.material + " " + me.name;
        for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        bool additive = (mt && mt->additive) || low.find("glow") != std::string::npos;
        bool blend = additive || (mt && mt->alphaBlend) || low.find("glass") != std::string::npos ||
                     low.find("window") != std::string::npos;
        bool gloss = low.find("_mm_ext") != std::string::npos || low.find("paint") != std::string::npos ||
                     low.find("chrome") != std::string::npos;
        float col[4] = { me.color[0], me.color[1], me.color[2], me.color[3] };
        if (mt && mt->hasColor && tex.empty()) for (int k = 0; k < 4; ++k) col[k] = mt->color[k];
        int t = textureFor(tex);
        std::string j = "{\"name\":" + jsonString(me.material.empty() ? me.name : me.material) +
                        ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" + num(col[0]) + "," + num(col[1]) + "," +
                        num(col[2]) + "," + num(blend && t < 0 ? std::min(col[3], 0.45f) : col[3]) + "]";
        if (t >= 0) j += ",\"baseColorTexture\":{\"index\":" + std::to_string(t) + "}";
        j += ",\"metallicFactor\":" + std::string(low.find("chrome") != std::string::npos ? "1" : "0") +
             ",\"roughnessFactor\":" + (gloss ? "0.3" : "0.65") + "}";
        if (additive) {
            j += ",\"emissiveFactor\":[1,1,1]";
            if (t >= 0) j += ",\"emissiveTexture\":{\"index\":" + std::to_string(t) + "}";
        }
        // the games' panels are single-sided shells with mixed winding
        j += ",\"doubleSided\":true";
        if (blend) j += ",\"alphaMode\":\"BLEND\"";
        j += "}";
        materialsJson.push_back(j);
        int idx = (int)materialsJson.size() - 1;
        materialIndex[key] = idx;
        return idx;
    };

    // ---- meshes ----
    std::vector<std::string> meshesJson;
    std::vector<std::string> nodesJson;            // filled after; meshes first
    std::vector<int> meshNode;                     // per kept mesh: its node
    for (const Mesh* mp : meshes) {
        const Mesh& me = *mp;
        size_t nv = me.positions.size() / 3;
        std::string attrs = "\"POSITION\":" + std::to_string(bin.floats(me.positions, 3, true));
        if (me.normals.size() == nv * 3) {
            std::vector<float> n = me.normals;
            for (size_t i = 0; i < nv; ++i) {
                float* q = &n[i * 3];
                float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                if (l > 1e-8f) { q[0] /= l; q[1] /= l; q[2] /= l; } else { q[0] = 0; q[1] = 1; q[2] = 0; }
            }
            attrs += ",\"NORMAL\":" + std::to_string(bin.floats(n, 3, false));
        }
        // glTF's UV origin is the image's top-left corner
        auto uvSet = [&](const std::vector<float>& uv, const char* name) {
            if (uv.size() != nv * 2) return;
            std::vector<float> t = uv;
            for (size_t i = 1; i < t.size(); i += 2) t[i] = 1.0f - t[i];
            attrs += std::string(",\"") + name + "\":" + std::to_string(bin.floats(t, 2, false));
        };
        uvSet(me.uvs, "TEXCOORD_0");
        uvSet(me.uvs2, "TEXCOORD_1");
        if (me.colors.size() == nv * 4) attrs += ",\"COLOR_0\":" + std::to_string(bin.floats(me.colors, 4, false));
        std::vector<uint32_t> idx;
        idx.reserve(me.indices.size());
        for (size_t i = 0; i + 2 < me.indices.size(); i += 3)
            if (me.indices[i] < nv && me.indices[i + 1] < nv && me.indices[i + 2] < nv)
                idx.insert(idx.end(), { me.indices[i], me.indices[i + 1], me.indices[i + 2] });
        if (idx.empty()) { meshNode.push_back(-1); continue; }
        int ia = bin.indices(idx);
        int mat = materialFor(me);
        meshesJson.push_back("{\"name\":" + jsonString(me.name) + ",\"primitives\":[{\"attributes\":{" + attrs +
                             "},\"indices\":" + std::to_string(ia) + ",\"material\":" + std::to_string(mat) +
                             ",\"mode\":4}]}");
        nodesJson.push_back("{\"name\":" + jsonString(me.name) + ",\"mesh\":" +
                            std::to_string(meshesJson.size() - 1) + "}");
        meshNode.push_back((int)nodesJson.size() - 1);
    }

    // ---- the tree: root -> LOD -> part -> meshes, plus HARDPOINTS ----
    std::vector<std::string> lods;
    std::map<std::string, std::vector<std::string>> partsOf;          // lod -> parts in order
    std::map<std::string, std::vector<int>> nodesOfGroup;             // lod \x1f part -> mesh nodes
    for (size_t k = 0; k < meshes.size(); ++k) {
        if (meshNode[k] < 0) continue;
        const Mesh& me = *meshes[k];
        std::string l = me.lod.empty() ? "MESHES" : me.lod;
        std::string p = me.part.empty() ? "parts" : me.part;
        if (std::find(lods.begin(), lods.end(), l) == lods.end()) lods.push_back(l);
        auto& ps = partsOf[l];
        if (std::find(ps.begin(), ps.end(), p) == ps.end()) ps.push_back(p);
        nodesOfGroup[l + "\x1f" + p].push_back(meshNode[k]);
    }
    std::sort(lods.begin(), lods.end());
    auto childList = [](const std::vector<int>& c) {
        std::string s = "[";
        for (size_t i = 0; i < c.size(); ++i) s += (i ? "," : "") + std::to_string(c[i]);
        return s + "]";
    };
    std::vector<int> rootChildren;
    for (const std::string& l : lods) {
        std::vector<int> lodChildren;
        for (const std::string& p : partsOf[l]) {
            nodesJson.push_back("{\"name\":" + jsonString(p) + ",\"children\":" +
                                childList(nodesOfGroup[l + "\x1f" + p]) + "}");
            lodChildren.push_back((int)nodesJson.size() - 1);
        }
        nodesJson.push_back("{\"name\":" + jsonString(l) + ",\"children\":" + childList(lodChildren) + "}");
        rootChildren.push_back((int)nodesJson.size() - 1);
    }
    if (!src.points.empty()) {
        std::vector<int> pts;
        for (const Hardpoint& h : src.points) {
            std::string j = "{\"name\":" + jsonString(h.name) + ",\"translation\":[" + num(h.pos[0]) + "," +
                            num(h.pos[1]) + "," + num(h.pos[2]) + "]";
            if (h.hinge) {
                j += ",\"extras\":{\"basis\":[";
                for (int k = 0; k < 9; ++k) j += (k ? "," : "") + num(h.basis[k]);
                j += "]}";
            }
            nodesJson.push_back(j + "}");
            pts.push_back((int)nodesJson.size() - 1);
        }
        nodesJson.push_back("{\"name\":\"HARDPOINTS\",\"children\":" + childList(pts) + "}");
        rootChildren.push_back((int)nodesJson.size() - 1);
    }
    std::string rootName = sceneName.empty() ? std::string("model") : sceneName;
    nodesJson.push_back("{\"name\":" + jsonString(rootName) + ",\"children\":" + childList(rootChildren) + "}");
    int root = (int)nodesJson.size() - 1;

    // ---- the JSON ----
    while (bin.data.size() % 4) bin.data.push_back(0);
    auto arr = [](const std::vector<std::string>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
        return s + "]";
    };
    std::string j = "{\"asset\":{\"version\":\"2.0\",\"generator\":" + jsonString(kVersion) + "}";
    j += ",\"scene\":0,\"scenes\":[{\"name\":" + jsonString(rootName) + ",\"nodes\":[" + std::to_string(root) + "]}]";
    j += ",\"nodes\":" + arr(nodesJson);
    if (!meshesJson.empty()) j += ",\"meshes\":" + arr(meshesJson);
    if (!materialsJson.empty()) j += ",\"materials\":" + arr(materialsJson);
    if (!texturesJson.empty()) {
        // repeat: UVs past 0..1 tile (the cockpit trim's grain does)
        j += ",\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}]";
        j += ",\"textures\":" + arr(texturesJson) + ",\"images\":" + arr(imagesJson);
    }
    if (!bin.accessors.empty()) j += ",\"accessors\":" + arr(bin.accessors);
    if (!bin.views.empty()) j += ",\"bufferViews\":" + arr(bin.views);
    if (!bin.data.empty()) j += ",\"buffers\":[{\"byteLength\":" + std::to_string(bin.data.size()) + "}]";
    j += "}";
    while (j.size() % 4) j += ' ';

    // ---- the container: header, JSON chunk, BIN chunk ----
    auto put32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back((uint8_t)(v >> (8 * k))); };
    uint32_t total = 12 + 8 + (uint32_t)j.size() + (bin.data.empty() ? 0 : 8 + (uint32_t)bin.data.size());
    put32(0x46546C67);              // glTF
    put32(2);
    put32(total);
    put32((uint32_t)j.size());
    put32(0x4E4F534A);              // JSON
    out.insert(out.end(), j.begin(), j.end());
    if (!bin.data.empty()) {
        put32((uint32_t)bin.data.size());
        put32(0x004E4942);          // BIN
        out.insert(out.end(), bin.data.begin(), bin.data.end());
    }
    if (note) {
        char b[200];
        snprintf(b, sizeof(b), "%zu mesh(es), %d texture(s) inside the file%s%s", meshesJson.size(), embedded,
                 linked ? ", some linked as PNG beside it" : "",
                 helpers ? "; HELPERS (collision, decal shell) left out" : "");
        *note = b;
    }
    return true;
}

} // namespace nfsnl
