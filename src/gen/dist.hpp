#pragma once
#include <cstdint>
#include <string>

// Value distributions for vector noise (AO / raytrace directions).
// Samples = Fibonacci point set (near perfect spread), shuffled by seed.
// Swap optimizer then only moves whole vectors -> distribution stays exact.
// Stored encoded to 0..1: signed component c -> c * 0.5 + 0.5. z up (+z) for hemi.
enum class Dist {
    Uniform,   // per channel uniform 0..1 (plain blue noise)
    Disk,      // vec2 uniform in unit disk
    Circle,    // vec2 unit length (2D direction)
    Sphere,    // vec3 unit sphere
    Hemi,      // vec3 uniform hemisphere
    CosHemi,   // vec3 cosine weighted hemisphere (AO / diffuse rays)
};

bool parseDist(const std::string& s, Dist& d);
const char* distName(Dist d);
int distChannels(Dist d);   // 0 = any (Uniform)

// n samples, encoded, into planes[0..channels). shuffled with (seed, stream).
void fillDist(Dist d, int n, uint64_t seed, uint64_t stream, float* const* planes);
