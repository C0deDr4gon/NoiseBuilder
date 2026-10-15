#include "io/tga.hpp"
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

void put16(uint8_t* p, int v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

int get16(const uint8_t* p) { return p[0] | (p[1] << 8); }

// Pixel bytes per channel count. 2 ch stored as 24-bit.
int bytesPerPixel(int c) { return c == 1 ? 1 : c == 4 ? 4 : 3; }

// Write header + rows of slices [z0, z1) stacked.
bool writeRange(const std::string& path, const Image& img, int z0, int z1, std::string& err) {
    if (img.c < 1 || img.c > 4) { err = "tga: channels must be 1..4"; return false; }
    int outH = img.h * (z1 - z0);
    if (img.w > 65535 || outH > 65535) { err = "tga: image too big (max 65535)"; return false; }

    int bpp = bytesPerPixel(img.c);
    uint8_t hdr[18] = {};
    hdr[2] = img.c == 1 ? 3 : 2;             // 3 = gray, 2 = truecolor
    put16(hdr + 12, img.w);
    put16(hdr + 14, outH);
    hdr[16] = (uint8_t)(bpp * 8);
    hdr[17] = 0x20 | (img.c == 4 ? 8 : 0);  // top-left origin, alpha bits

    std::vector<uint8_t> row((size_t)img.w * bpp);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { err = "tga: cannot open " + path; return false; }
    bool ok = std::fwrite(hdr, 1, 18, f) == 18;

    for (int z = z0; z < z1 && ok; ++z) {
        for (int y = 0; y < img.h && ok; ++y) {
            uint8_t* p = row.data();
            for (int x = 0; x < img.w; ++x) {
                if (img.c == 1) {
                    *p++ = toByte(img.at(x, y, z, 0));
                } else {
                    // file order is B G R (A)
                    uint8_t r = toByte(img.at(x, y, z, 0));
                    uint8_t g = toByte(img.at(x, y, z, 1));
                    uint8_t b = img.c >= 3 ? toByte(img.at(x, y, z, 2)) : 0;
                    *p++ = b; *p++ = g; *p++ = r;
                    if (img.c == 4) *p++ = toByte(img.at(x, y, z, 3));
                }
            }
            ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
        }
    }
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

    uint8_t hdr[18];
    if (std::fread(hdr, 1, 18, f) != 18) { std::fclose(f); err = "tga: short header"; return false; }

    int idLen = hdr[0], cmapType = hdr[1], type = hdr[2];
    int w = get16(hdr + 12), h = get16(hdr + 14), bits = hdr[16];
    bool topLeft = (hdr[17] & 0x20) != 0;
    int bpp = bits / 8;

    bool okType = cmapType == 0 &&
                  ((type == 3 && bits == 8) || (type == 2 && (bits == 24 || bits == 32)));
    if (!okType || w == 0 || h == 0) { std::fclose(f); err = "tga: unsupported format"; return false; }

    std::fseek(f, idLen, SEEK_CUR);
    std::vector<uint8_t> px((size_t)w * h * bpp);
    bool ok = std::fread(px.data(), 1, px.size(), f) == px.size();
    std::fclose(f);
    if (!ok) { err = "tga: short pixel data"; return false; }

    int c = bpp == 1 ? 1 : bpp == 4 ? 4 : 3;
    if (bpp == 3) {
        bool blueZero = true;
        for (size_t i = 0; i < px.size() && blueZero; i += 3) blueZero = px[i] == 0;
        if (blueZero) c = 2;
    }

    out = Image(w, h, 1, c);
    for (int y = 0; y < h; ++y) {
        int fy = topLeft ? y : h - 1 - y;
        const uint8_t* p = px.data() + (size_t)fy * w * bpp;
        for (int x = 0; x < w; ++x, p += bpp) {
            if (bpp == 1) {
                out.at(x, y, 0, 0) = fromByte(p[0]);
            } else {
                out.at(x, y, 0, 0) = fromByte(p[2]);              // R
                out.at(x, y, 0, 1) = fromByte(p[1]);              // G
                if (c >= 3) out.at(x, y, 0, 2) = fromByte(p[0]);  // B
                if (c == 4) out.at(x, y, 0, 3) = fromByte(p[3]);  // A
            }
        }
    }
    return true;
}
