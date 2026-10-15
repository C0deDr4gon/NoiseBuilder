#include "gen/dist.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include "rng.hpp"

bool parseDist(const std::string& s, Dist& d) {
    if (s == "uniform") d = Dist::Uniform;
    else if (s == "disk") d = Dist::Disk;
    else if (s == "circle") d = Dist::Circle;
    else if (s == "sphere") d = Dist::Sphere;
    else if (s == "hemi") d = Dist::Hemi;
    else if (s == "coshemi") d = Dist::CosHemi;
    else return false;
    return true;
}

const char* distName(Dist d) {
    switch (d) {
        case Dist::Uniform: return "uniform";
        case Dist::Disk:    return "disk";
        case Dist::Circle:  return "circle";
        case Dist::Sphere:  return "sphere";
        case Dist::Hemi:    return "hemi";
        case Dist::CosHemi: return "coshemi";
    }
    return "?";
}

int distChannels(Dist d) {
    switch (d) {
        case Dist::Uniform: return 0;
        case Dist::Disk:
        case Dist::Circle:  return 2;
        default:            return 3;
    }
}

void fillDist(Dist d, int n, uint64_t seed, uint64_t stream, float* const* planes) {
    const int c = distChannels(d);
    const double pi2 = 6.283185307179586;
    const double phi = 0.6180339887498949;   // golden ratio fraction
    std::vector<float> s((size_t)n * 3, 0.0f);

    for (int i = 0; i < n; ++i) {
        double u = (i + 0.5) / n;                    // stratified
        double v = std::fmod(i * phi, 1.0);          // golden ratio
        double x = 0, y = 0, z = 0;
        switch (d) {
            case Dist::Disk: {
                double r = std::sqrt(u);
                x = r * std::cos(pi2 * v); y = r * std::sin(pi2 * v);
                break;
            }
            case Dist::Circle:
                x = std::cos(pi2 * u); y = std::sin(pi2 * u);
                break;
            case Dist::Sphere: {
                z = 1.0 - 2.0 * u;
                double r = std::sqrt(std::max(0.0, 1.0 - z * z));
                x = r * std::cos(pi2 * v); y = r * std::sin(pi2 * v);
                break;
            }
            case Dist::Hemi: {
                z = u;
                double r = std::sqrt(std::max(0.0, 1.0 - z * z));
                x = r * std::cos(pi2 * v); y = r * std::sin(pi2 * v);
                break;
            }
            case Dist::CosHemi: {
                double r = std::sqrt(u);                 // uniform disk ...
                x = r * std::cos(pi2 * v); y = r * std::sin(pi2 * v);
                z = std::sqrt(std::max(0.0, 1.0 - u));   // ... lifted = cosine weighted
                break;
            }
            case Dist::Uniform: break;
        }
        s[(size_t)i * 3 + 0] = (float)(x * 0.5 + 0.5);
        s[(size_t)i * 3 + 1] = (float)(y * 0.5 + 0.5);
        s[(size_t)i * 3 + 2] = (float)(z * 0.5 + 0.5);
    }

    // shuffle whole vectors
    Rng rng(seed, stream);
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int i = n - 1; i > 0; --i) std::swap(order[i], order[rng.below(i + 1)]);

    for (int i = 0; i < n; ++i)
        for (int k = 0; k < c; ++k) planes[k][i] = s[(size_t)order[i] * 3 + k];
}
