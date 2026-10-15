#include "io/tga.hpp"
#include <cstdio>
#include <vector>

extern "C" {
#include "../../ext/libtex/libtex.h"
}

namespace {

void init() {
    static const bool ready = (tex_init(), true);
    (void)ready;
}

// Write header + rows of slices [z0, z1) stacked. 2 ch -> 24 bit, B = 0.
bool writeRange(const std::string& path, const Image& img, int z0, int z1, std::string& err) {
    if (img.c < 1 || img.c > 4) { err = "tga: channels must be 1..4"; return false; }
    int outH = img.h * (z1 - z0);
    if (img.w > 65535 || outH > 65535) { err = "tga: image too big (max 65535)"; return false; }
    init();

    int n = img.c == 1 ? 1 : img.c == 4 ? 4 : 3;
    size_t count = (size_t)img.w * outH;
    std::vector<float> rgba(count * 4, 0.0f);
    for (int z = z0; z < z1; ++z)
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                float* p = rgba.data() + (((size_t)(z - z0) * img.h + y) * img.w + x) * 4;
                for (int c = 0; c < img.c; ++c) p[c] = img.at(x, y, z, c);
                if (img.c < 4) p[3] = 1.0f;
            }

    std::vector<unsigned char> file(18 + count * n), row((size_t)img.w * n);
    int head = tex_tga_header(img.w, outH, n, 0, file.data());
    int size = tex_tga_pixels(rgba.data(), img.w, outH, n, 0, file.data() + head, row.data());
    file.resize((size_t)head + size);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "tga: cannot open " + path; return false; }
    bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) err = "tga: write failed " + path;
    return ok;
}

} // namespace

std::string slicePath(const std::string& path, int z) {
    std::string base = path, ext;
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        base = path.substr(0, dot);
        ext = path.substr(dot);
    }
    char num[16];
    std::snprintf(num, sizeof num, "_%03d", z);
    return base + num + ext;
}

bool writeTga(const std::string& path, const Image& img, int z, std::string& err) {
    if (z < 0 || z >= img.d) { err = "tga: bad slice"; return false; }
    return writeRange(path, img, z, z + 1, err);
}

bool writeTgaSlices(const std::string& path, const Image& img, std::string& err) {
    if (img.d == 1) return writeTga(path, img, 0, err);
    for (int z = 0; z < img.d; ++z)
        if (!writeTga(slicePath(path, z), img, z, err)) return false;
    return true;
}

bool writeTgaAtlas(const std::string& path, const Image& img, std::string& err) {
    return writeRange(path, img, 0, img.d, err);
}

bool readTga(const std::string& path, Image& out, std::string& err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "tga: cannot open " + path; return false; }
    std::vector<unsigned char> file;
    unsigned char buf[65536];
    for (size_t got; (got = std::fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + got);
    std::fclose(f);
    init();

    tex_info info;
    int len = (int)file.size();
    if (file.size() > 0x7fffffff || tex_tga_info(file.data(), len, &info) != TEX_OK) { err = "tga: unsupported format"; return false; }
    std::vector<float> rgba((size_t)info.width * info.height * 4);
    std::vector<unsigned char> scratch((size_t)info.scratch + 1);
    if (tex_tga_read(file.data(), len, rgba.data(), scratch.data()) != TEX_OK) { err = "tga: bad pixel data"; return false; }

    int c = (file[2] & 7) == 3 ? 1 : file[16] == 32 ? 4 : 3;
    if (c == 3) {
        bool blueZero = true;
        for (size_t i = 2; i < rgba.size() && blueZero; i += 4) blueZero = rgba[i] == 0.0f;
        if (blueZero) c = 2;
    }

    out = Image(info.width, info.height, 1, c);
    for (int y = 0; y < info.height; ++y)
        for (int x = 0; x < info.width; ++x)
            for (int k = 0; k < c; ++k)
                out.at(x, y, 0, k) = rgba[((size_t)y * info.width + x) * 4 + k];
    return true;
}
