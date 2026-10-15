#pragma once
#include <string>
#include "image.hpp"

// Own TGA, uncompressed only. No deps.
// Float value 0..1 -> byte = floor(v * 256), clamp 255.
// (value = rank / N and N multiple of 256 -> every byte used equal times.)
//
// Channels -> TGA type:
//   1 ch -> type 3, 8-bit gray
//   2 ch -> type 2, 24-bit, R = ch0, G = ch1, B = 0
//   3 ch -> type 2, 24-bit, R G B = ch0..ch2 (vector dists)
//   4 ch -> type 2, 32-bit, R G B A = ch0..ch3
// Origin top-left (row 0 first in file).

// One slice z of img -> file. Return false + err on fail.
bool writeTga(const std::string& path, const Image& img, int z, std::string& err);

// All slices. d == 1 -> path as is. d > 1 -> "name_000.tga", "name_001.tga", ...
bool writeTgaSlices(const std::string& path, const Image& img, std::string& err);

// All slices stacked top to bottom in one W x (H*D) file.
bool writeTgaAtlas(const std::string& path, const Image& img, std::string& err);

// Read own files back (types 2 / 3, 8/24/32 bit, uncompressed, any origin).
// 24-bit with B all zero -> 2 ch. Other 24-bit -> 3 ch. Values = byte / 256 + half step.
bool readTga(const std::string& path, Image& out, std::string& err);

// "dir/name.tga", 7 -> "dir/name_007.tga"
std::string slicePath(const std::string& path, int z);
