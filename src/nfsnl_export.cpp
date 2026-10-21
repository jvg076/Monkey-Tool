// nfsnl_export.cpp - the Export window's settings applied: model scale and
// detail levels, texture formats with their compression (PNG zlib level,
// DDS BC1/BC2/BC3/BC7/RGBA8, Real Racing 3's own ETC1 / ATC / RGBA4444) and
// sounds to Ogg Vorbis.
//
// Third-party code compiled in for this (src/thirdparty, see
// THIRD_PARTY_NOTICES.md): miniz (MIT) for zlib levels, bc7enc (MIT / public
// domain) for BC7, libogg + libvorbis (BSD) for Vorbis.
#include "nfsnl.h"
#include "thirdparty/bc7enc/bc7enc.h"
#include "thirdparty/miniz/miniz.h"
#include "thirdparty/vorbis/vorbis/codec.h"
#include "thirdparty/vorbis/vorbis/vorbisenc.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <mutex>

namespace nfsnl {

std::vector<std::string> exportUnitNames() {
    return { "Game units (1 unit = 1 m, as before)", "Metres", "Centimetres", "Millimetres", "Inches" };
}
std::vector<std::string> ddsFormatNames() {
    return { "BC1 / DXT1 (RGB, 1-bit alpha)", "BC2 / DXT3 (sharp alpha)", "BC3 / DXT5 (smooth alpha)",
             "BC7 (best quality)", "R8G8B8A8_UNORM (uncompressed)" };
}
std::vector<std::string> rr3GpuNames() {
    return { "ETC1 - every Android phone: Mali, Xclipse, PowerVR, Adreno (.etc.dds.z)",
             "ATC - Adreno build (.atc.dds.z)",
             "RGBA4444 uncompressed - any phone, 4x larger (.etc.dds.z)" };
}

// ---------------------------------------------------------------- models

namespace {
// numbers multiplied into the geometry, and what the FBX header then says a
// unit is (centimetres per unit)
void unitFactors(int unit, float& mul, double& fbx) {
    switch (unit) {
        case 1: mul = 1.0f;     fbx = 100.0; break;     // metres, stated as metres
        case 2: mul = 100.0f;   fbx = 1.0;   break;     // centimetres
        case 3: mul = 1000.0f;  fbx = 0.1;   break;     // millimetres
        case 4: mul = 39.3701f; fbx = 2.54;  break;     // inches
        default: mul = 1.0f;    fbx = 1.0;   break;     // as every version before 1.3.1 wrote it
    }
}
} // namespace

void applyModelExportSettings(Model& m, const ExportSettings& s) {
    if (!s.helpers)
        m.meshes.erase(std::remove_if(m.meshes.begin(), m.meshes.end(),
                                      [](const Mesh& me) { return me.lod == "HELPERS"; }),
                       m.meshes.end());
    if (s.lods == 1) {
        // the most detailed level (LOD00, else the lowest number there is);
        // damage, camera views and helpers go too, as Frosty's single LOD does
        std::string best;
        for (const Mesh& me : m.meshes)
            if (me.lod.compare(0, 3, "LOD") == 0 && (best.empty() || me.lod < best)) best = me.lod;
        if (!best.empty())
            m.meshes.erase(std::remove_if(m.meshes.begin(), m.meshes.end(),
                                          [&](const Mesh& me) { return !me.lod.empty() && me.lod != best; }),
                           m.meshes.end());
    }
    float mul = 1.0f;
    double fbx = 1.0;
    unitFactors(s.unit, mul, fbx);
    m.fbxUnitScale = fbx;
    if (mul == 1.0f) return;
    for (Mesh& me : m.meshes) {
        for (float& v : me.positions) v *= mul;
        for (int c = 0; c < 3; ++c) { me.bboxMin[c] *= mul; me.bboxMax[c] *= mul; }
        if (me.placeRotated || me.placeMove[0] || me.placeMove[1] || me.placeMove[2])
            for (float& v : me.placeMove) v *= mul;
    }
    for (Hardpoint& h : m.points)
        for (float& v : h.pos) v *= mul;
    // the moving parts' slides (channels 0..2 are distances)
    for (PartAnim& a : m.anims)
        for (PartAnim::Node& n : a.nodes)
            for (int c = 0; c < 3; ++c)
                for (auto& k : n.ch[c]) k.second *= mul;
}

// ---------------------------------------------------------------- textures

Bytes encodePngLevel(const Image& img, int level) {
    if (!img.ok()) return Bytes();
    level = std::max(0, std::min(9, level));
    int ch = img.channels;
    Image src = img;
    if (ch == 2) { src = toRgba(img); ch = 4; }
    size_t len = 0;
    void* p = tdefl_write_image_to_png_file_in_memory_ex(src.pixels.data(), src.width, src.height, ch, &len,
                                                         (mz_uint)level, MZ_FALSE);
    if (!p) return encodePng(img);
    Bytes out((const uint8_t*)p, (const uint8_t*)p + len);
    mz_free(p);
    return out;
}

namespace {

// the halved picture, each pixel the mean of the four under it
Image halve(const Image& a) {
    Image b;
    b.width = std::max(1, a.width / 2);
    b.height = std::max(1, a.height / 2);
    b.channels = 4;
    b.pixels.resize((size_t)b.width * b.height * 4);
    for (int y = 0; y < b.height; ++y)
        for (int x = 0; x < b.width; ++x)
            for (int c = 0; c < 4; ++c) {
                int s = 0, n = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        int sx = std::min(a.width - 1, x * 2 + dx), sy = std::min(a.height - 1, y * 2 + dy);
                        s += a.pixels[((size_t)sy * a.width + sx) * 4 + c];
                        ++n;
                    }
                b.pixels[((size_t)y * b.width + x) * 4 + c] = (uint8_t)((s + n / 2) / n);
            }
    return b;
}

std::vector<Image> mipChain(const Image& rgba, bool all) {
    std::vector<Image> out{ rgba };
    if (!all) return out;
    while (out.back().width > 1 || out.back().height > 1) out.push_back(halve(out.back()));
    return out;
}

// block formats need whole 4x4 blocks: the edge pixels repeated
Image padTo4(const Image& a) {
    int w = (a.width + 3) & ~3, h = (a.height + 3) & ~3;
    if (w == a.width && h == a.height) return a;
    Image b;
    b.width = w; b.height = h; b.channels = 4;
    b.pixels.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            memcpy(&b.pixels[((size_t)y * w + x) * 4],
                   &a.pixels[((size_t)std::min(y, a.height - 1) * a.width + std::min(x, a.width - 1)) * 4], 4);
    return b;
}

Bytes encodeBc7(const Image& rgba) {
    static std::once_flag once;
    std::call_once(once, [] { bc7enc_compress_block_init(); });
    Image p = padTo4(rgba);
    int bw = p.width / 4, bh = p.height / 4;
    Bytes out((size_t)bw * bh * 16);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    params.m_max_partitions_mode = 16;      // quick, and still far better than BC3
    params.m_uber_level = 0;
    uint8_t block[64];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y)
                memcpy(block + y * 16, &p.pixels[((size_t)(by * 4 + y) * p.width + bx * 4) * 4], 16);
            bc7enc_compress_block(&out[((size_t)by * bw + bx) * 16], block, &params);
        }
    return out;
}

void put32(Bytes& b, size_t off, uint32_t v) { memcpy(b.data() + off, &v, 4); }

} // namespace

bool encodeDdsFile(const Image& img, int fmt, bool mipmaps, Bytes& out, std::string* err) {
    if (!img.ok()) { if (err) *err = "no picture"; return false; }
    Image rgba = toRgba(img);
    std::vector<Image> levels = mipChain(rgba, mipmaps);
    bool dx10 = fmt == 3 || fmt == 4;
    Bytes body;
    for (const Image& lv : levels) {
        Bytes b;
        switch (fmt) {
            case 0: b = encodeDxt(padTo4(lv), 1); break;
            case 1: b = encodeDxt(padTo4(lv), 3); break;
            case 2: b = encodeDxt(padTo4(lv), 5); break;
            case 3: b = encodeBc7(lv); break;
            default: b = lv.pixels; break;
        }
        body.insert(body.end(), b.begin(), b.end());
    }
    out.assign(128 + (dx10 ? 20 : 0), 0);
    memcpy(out.data(), "DDS ", 4);
    put32(out, 4, 124);
    uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000 | (levels.size() > 1 ? 0x20000 : 0);
    bool block = fmt != 4;
    flags |= block ? 0x80000 : 0x8;                      // linear size / pitch
    put32(out, 8, flags);
    put32(out, 12, (uint32_t)rgba.height);
    put32(out, 16, (uint32_t)rgba.width);
    uint32_t first = block ? (uint32_t)(((rgba.width + 3) / 4) * ((rgba.height + 3) / 4) * (fmt == 0 ? 8 : 16))
                           : (uint32_t)rgba.width * 4;
    put32(out, 20, first);
    put32(out, 28, (uint32_t)levels.size());
    put32(out, 76, 32);
    put32(out, 80, 0x4);                                 // FourCC
    const char* cc = fmt == 0 ? "DXT1" : fmt == 1 ? "DXT3" : fmt == 2 ? "DXT5" : "DX10";
    memcpy(out.data() + 84, cc, 4);
    put32(out, 108, 0x1000 | (levels.size() > 1 ? 0x400008 : 0));
    if (dx10) {
        put32(out, 128, fmt == 3 ? 98u : 28u);           // DXGI_FORMAT_BC7_UNORM / R8G8B8A8_UNORM
        put32(out, 132, 3);                              // 2D texture
        put32(out, 136, 0);
        put32(out, 140, 1);                              // array size
        put32(out, 144, 0);
    }
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

bool encodeRr3GameTexture(const Image& img, int gpu, Bytes& out, std::string& ext) {
    if (!img.ok()) return false;
    Image rgba = toRgba(img);
    // the game keeps its pictures bottom row first (the tool turns them the
    // right way up when it reads them), so they go back that way
    flipImageVertically(rgba);
    std::vector<Image> levels = mipChain(rgba, true);
    bool alpha = false;
    for (size_t i = 3; i < rgba.pixels.size(); i += 4) if (rgba.pixels[i] < 250) { alpha = true; break; }
    // ETC1 has no alpha: a picture that needs it goes uncompressed, as the
    // game's own badge sheets are
    if (gpu == 0 && alpha) gpu = 2;
    Bytes body;
    for (const Image& lv : levels) {
        Bytes b;
        if (gpu == 0) b = encodeEtc1(padTo4(lv));
        else if (gpu == 1) b = encodeAtc(padTo4(lv), alpha ? 5 : 1);
        else {
            b.resize((size_t)lv.width * lv.height * 2);
            for (size_t i = 0; i < (size_t)lv.width * lv.height; ++i) {
                const uint8_t* p = &lv.pixels[i * 4];
                uint16_t v = (uint16_t)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
                memcpy(&b[i * 2], &v, 2);
            }
        }
        body.insert(body.end(), b.begin(), b.end());
    }
    // the header exactly as the game's own files have it
    Bytes dds(128, 0);
    memcpy(dds.data(), "DDS ", 4);
    put32(dds, 4, 124);
    put32(dds, 12, (uint32_t)rgba.height);
    put32(dds, 16, (uint32_t)rgba.width);
    put32(dds, 28, (uint32_t)levels.size());
    put32(dds, 76, 32);
    put32(dds, 108, 0x401008);
    if (gpu == 2) {
        put32(dds, 8, 0x21007);
        put32(dds, 80, 0x41);                            // RGB + alpha pixels
        put32(dds, 88, 16);
        put32(dds, 92, 0xF000); put32(dds, 96, 0x0F00); put32(dds, 100, 0x00F0); put32(dds, 104, 0x000F);
    } else {
        put32(dds, 8, 0xA1007);
        uint32_t bpp = gpu == 1 && alpha ? 8 : 4;
        put32(dds, 20, (uint32_t)((size_t)rgba.width * rgba.height * bpp / 8));
        put32(dds, 80, 0x5);
        memcpy(dds.data() + 84, gpu == 0 ? "ETC " : alpha ? "ATCI" : "ATC ", 4);
    }
    dds.insert(dds.end(), body.begin(), body.end());
    out = rr3WrapZ(dds);
    ext = gpu == 1 ? "atc.dds.z" : "etc.dds.z";
    return true;
}

bool encodeImageAs(const Image& img, const ExportSettings& s, Bytes& out, std::string& ext, std::string* err) {
    const std::string& f = s.textureFormat;
    if (f == "jpg") { out = encodeJpeg(img, std::max(10, std::min(100, s.jpgQuality))); ext = "jpg"; }
    else if (f == "bmp") { out = encodeBmp(img); ext = "bmp"; }
    else if (f == "tga") { out = encodeTga(img); ext = "tga"; }
    else if (f == "dds") { ext = "dds"; return encodeDdsFile(img, s.ddsFormat, s.mipmaps, out, err); }
    else if (f == "rr3") return encodeRr3GameTexture(img, s.rr3Gpu, out, ext);
    else { out = encodePngLevel(img, s.pngLevel); ext = "png"; }
    if (out.empty() && err) *err = "the picture did not encode";
    return !out.empty();
}

// ---------------------------------------------------------------- sounds

bool wavToOgg(const Bytes& wav, int quality, Bytes& out, std::string* err) {
    auto fail = [&](const char* why) { if (err) *err = why; return false; };
    if (wav.size() < 44 || memcmp(wav.data(), "RIFF", 4) || memcmp(wav.data() + 8, "WAVE", 4))
        return fail("not a .wav");
    int channels = 0, rate = 0, bits = 0, format = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmLen = 0;
    size_t p = 12;
    while (p + 8 <= wav.size()) {
        uint32_t sz;
        memcpy(&sz, wav.data() + p + 4, 4);
        const uint8_t* body = wav.data() + p + 8;
        if (p + 8 + sz > wav.size()) sz = (uint32_t)(wav.size() - p - 8);
        if (!memcmp(wav.data() + p, "fmt ", 4) && sz >= 16) {
            format = body[0] | body[1] << 8;
            channels = body[2] | body[3] << 8;
            memcpy(&rate, body + 4, 4);
            bits = body[14] | body[15] << 8;
        } else if (!memcmp(wav.data() + p, "data", 4)) {
            pcm = body;
            pcmLen = sz;
        }
        p += 8 + sz + (sz & 1);
    }
    if ((format != 1 && format != 0xFFFE) || bits != 16 || channels < 1 || channels > 8 || rate <= 0 || !pcm)
        return fail("only 16-bit PCM .wav converts to Ogg");
    size_t frames = pcmLen / (2 * (size_t)channels);

    vorbis_info vi;
    vorbis_info_init(&vi);
    float q = std::max(-1, std::min(10, quality)) / 10.0f;
    if (vorbis_encode_init_vbr(&vi, channels, rate, q) != 0) {
        vorbis_info_clear(&vi);
        return fail("the Vorbis encoder does not take this rate / channel count");
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    vorbis_comment_add_tag(&vc, "ENCODER", "Monkey Tool (libvorbis)");
    vorbis_dsp_state vd;
    vorbis_block vb;
    vorbis_analysis_init(&vd, &vi);
    vorbis_block_init(&vd, &vb);
    ogg_stream_state os;
    ogg_stream_init(&os, (int)(std::time(nullptr) & 0x7fffffff));
    ogg_page og;
    ogg_packet op;
    auto writePage = [&]() {
        out.insert(out.end(), og.header, og.header + og.header_len);
        out.insert(out.end(), og.body, og.body + og.body_len);
    };
    {
        ogg_packet h, hc, hcode;
        vorbis_analysis_headerout(&vd, &vc, &h, &hc, &hcode);
        ogg_stream_packetin(&os, &h);
        ogg_stream_packetin(&os, &hc);
        ogg_stream_packetin(&os, &hcode);
        while (ogg_stream_flush(&os, &og)) writePage();
    }
    const size_t chunk = 1024;
    size_t done = 0;
    bool eos = false;
    while (!eos) {
        size_t n = std::min(chunk, frames - done);
        if (n == 0) {
            vorbis_analysis_wrote(&vd, 0);
        } else {
            float** buf = vorbis_analysis_buffer(&vd, (int)n);
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < channels; ++c) {
                    int16_t s;
                    memcpy(&s, pcm + ((done + i) * channels + c) * 2, 2);
                    buf[c][i] = s / 32768.0f;
                }
            vorbis_analysis_wrote(&vd, (int)n);
            done += n;
        }
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, nullptr);
            vorbis_bitrate_addblock(&vb);
            while (vorbis_bitrate_flushpacket(&vd, &op)) {
                ogg_stream_packetin(&os, &op);
                while (ogg_stream_pageout(&os, &og)) {
                    writePage();
                    if (ogg_page_eos(&og)) eos = true;
                }
            }
        }
        if (n == 0) {
            // the last packets, then the stream's end
            while (ogg_stream_flush(&os, &og)) { writePage(); if (ogg_page_eos(&og)) eos = true; }
            eos = true;
        }
    }
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    return !out.empty();
}

unsigned exportKindOf(const std::string& path) {
    std::string e = extensionOf(path);
    std::string bare = e == "z" ? extensionOf(stripExtension(path)) : e;
    if (bare == "sb3d" || bare == "m3g" || bare == "obj" || bare == "fbx") return 1;
    if (path.find(".scene_static.sba") != std::string::npos && path.find("lightprobes") == std::string::npos &&
        path.find("exclusions") == std::string::npos && path.find("lightmaps") == std::string::npos)
        return 1;
    if (bare == "sba" || bare == "pvr" || bare == "dds" || bare == "ktx" || bare == "png" || bare == "jpg" ||
        bare == "jpeg" || bare == "tga" || bare == "bmp" || bare == "astc")
        return 2;
    if (bare == "wem" || bare == "gnsu" || bare == "sps" || bare == "wav" || bare == "ogg" || bare == "bnk" ||
        bare == "mp3" || bare == "snr" || bare == "sns")
        return 4;
    std::string k = classifyByName(path);
    if (k == "texture") return 2;
    if (k == "sound") return 4;
    if (k == "model") return 1;
    return 0;
}

bool convertAssetWithSettings(const std::string& path, const uint8_t* data, size_t len,
                              const ExportSettings& s, Bytes& out, std::string& ext,
                              std::string* err, bool* needsFfmpeg,
                              const uint8_t* sidecar, size_t sidecarLen) {
    if (needsFfmpeg) *needsFfmpeg = false;
    std::string own = extensionOf(path);
    unsigned kind = exportKindOf(path);
    auto raw = [&]() { out.assign(data, data + len); ext = own; return true; };
    if (kind == 1) {
        if (s.modelFormat == "raw") return raw();
        Model m = loadModel(data, len);
        if (!m.valid) {
            if (err) *err = m.warnings.empty() ? "no meshes" : m.warnings[0];
            return false;
        }
        if (sidecar && sidecarLen) {
            std::vector<Hardpoint> pts = rr3ReadPoints(sidecar, sidecarLen);
            if (!pts.empty()) rr3PlaceParts(m, pts);
        }
        if (!s.kitAll && !modelKits(m).empty()) keepKit(m, "stock");
        applyModelExportSettings(m, s);
        ext = s.modelFormat;
        return writeModel(m, s.modelFormat, out);
    }
    if (kind == 2) {
        if (s.textureFormat == "raw") return raw();
        Bytes png;
        std::string used;
        if (!convertAsset(path, data, len, "png", png, err, &used) || png.empty()) return false;
        Image img;
        if (used != "png" || !decodePng(png.data(), png.size(), img)) {
            // not a picture after all (a model-like .sba): what the tool made of it
            out.swap(png);
            ext = used.empty() ? own : used;
            return true;
        }
        return encodeImageAs(img, s, out, ext, err);
    }
    if (kind == 4) {
        if (s.audioFormat == "raw") return raw();
        Bytes wav;
        std::string used;
        if (!convertAsset(path, data, len, "wav", wav, err, &used) || wav.empty() || used != "wav") {
            // a bank or a sound this build cannot decode: the file itself
            if (own == "bnk" || wav.empty()) return raw();
            out.swap(wav);
            ext = used.empty() ? own : used;
            return true;
        }
        if (s.audioFormat == "ogg") {
            if (wavToOgg(wav, s.oggQuality, out, err)) { ext = "ogg"; return true; }
            // a .wav the Vorbis encoder does not take stays a .wav
        } else if (s.audioFormat == "mp3" || s.audioFormat == "aac" || s.audioFormat == "flac") {
            if (needsFfmpeg) *needsFfmpeg = true;
        }
        out.swap(wav);
        ext = "wav";
        return true;
    }
    // anything else: the tool's usual conversion for it, else the file
    std::string fmt = formatsFor(path).empty() ? std::string("raw") : formatsFor(path)[0];
    if (fmt != "raw" && convertAsset(path, data, len, fmt, out, err, &ext) && !out.empty()) return true;
    return raw();
}

} // namespace nfsnl
