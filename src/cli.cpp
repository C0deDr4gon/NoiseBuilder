#include "cli.hpp"
#include "simd.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

std::string Cli::get(const std::string& k, const std::string& def) const {
    auto it = opts.find(k);
    return it == opts.end() ? def : it->second;
}

// Strict parse: whole string must be a number, else badValue is set.
int Cli::getInt(const std::string& k, int def) const {
    auto it = opts.find(k);
    if (it == opts.end()) return def;
    const char* s = it->second.c_str();
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < -2147483647L || v > 2147483647L) {
        if (badValue.empty()) badValue = "--" + k + " needs an integer, got '" + it->second + "'";
        return def;
    }
    return (int)v;
}

float Cli::getFloat(const std::string& k, float def) const {
    auto it = opts.find(k);
    if (it == opts.end()) return def;
    const char* s = it->second.c_str();
    char* end = nullptr;
    float v = std::strtof(s, &end);
    if (end == s || *end != '\0' || !std::isfinite(v)) {
        if (badValue.empty()) badValue = "--" + k + " needs a number, got '" + it->second + "'";
        return def;
    }
    return v;
}

std::string Cli::unknownOption(const std::vector<std::string>& known) const {
    for (const auto& kv : opts) {
        bool ok = false;
        for (const auto& k : known) ok = ok || kv.first == k;
        if (!ok) return kv.first;
    }
    return "";
}

Cli parseCli(int argc, char** argv) {
    Cli cli;
    if (argc < 2) { cli.command = "help"; return cli; }

    cli.command = argv[1];
    if (cli.command == "-h" || cli.command == "--help") cli.command = "help";
    if (cli.command == "-v" || cli.command == "--version") cli.command = "version";

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() < 3 || a[0] != '-' || a[1] != '-') {
            cli.args.push_back(a);
            continue;
        }
        std::string key = a.substr(2);
        // value = next arg unless it is another --option (then flag = "1")
        if (i + 1 < argc && !(argv[i + 1][0] == '-' && argv[i + 1][1] == '-'))
            cli.opts[key] = argv[++i];
        else
            cli.opts[key] = "1";
    }
    return cli;
}

void printHelp() {
    std::printf("noisebuilder %s - blue noise texture generator (simd: %s, %d wide)\n\n",
                kVersion, simd::name(), simd::W);
    std::printf(
        "usage:\n"
        "  noisebuilder gen     [options]   make noise texture\n"
        "  noisebuilder analyze <file.tga>  check noise quality\n"
        "  noisebuilder help                this text\n"
        "  noisebuilder version             version + simd path\n"
        "\n"
        "gen options:\n"
        "  --algo     A             generator            (default vc)\n"
        "             vc        = void and cluster. best 1 ch. slices independent\n"
        "             swap      = swap optimizer. channels as vector. slices independent\n"
        "             stbn      = vc in 3D: blue in space + blue over time\n"
        "             stbn-swap = swap in 3D (--init vc starts from stbn)\n"
        "  --size     N             width = height, pot  (default 64)\n"
        "  --depth    N             time slices, pot     (default 1)\n"
        "  --channels 1|2|4         channels             (default 1)\n"
        "  --seed     N             rng seed             (default 1)\n"
        "  --sigma    F             spatial sigma        (default vc/stbn 1.9, swap 2.5)\n"
        "  --threads  N             0 = all cores        (default 0)\n"
        "  --out      file.tga      output file          (default noise.tga)\n"
        "  --atlas                  3D: one stacked file, not slices\n"
        "  --report   dir           also run analyze, files to dir\n"
        "\n"
        "swap / stbn-swap options:\n"
        "  --sigma-v  F             value sigma          (default 4.0)\n"
        "  --iters    N             swap tries per pixel (default 100)\n"
        "  --temp     F             anneal start temp    (default 0 = greedy)\n"
        "  --init     rand|vc       start pattern        (default rand)\n"
        "  --dist     D             value distribution   (default uniform)\n"
        "             uniform = per channel 0..1, uses --channels\n"
        "             disk    = vec2 in unit disk         (2 ch, RG)\n"
        "             circle  = vec2 unit direction       (2 ch, RG)\n"
        "             sphere  = vec3 unit sphere          (3 ch, RGB)\n"
        "             hemi    = vec3 uniform hemisphere   (3 ch, RGB, z up)\n"
        "             coshemi = vec3 cosine hemisphere    (3 ch, RGB, z up, for AO)\n"
        "             vectors stored as v * 0.5 + 0.5. decode: v = tex * 2 - 1\n"
        "\n"
        "stbn / stbn-swap options:\n"
        "  --sigma-t  F             time sigma           (default stbn 1.5, stbn-swap 2.1)\n"
        "  --time-weight F          time vs space        (default 1.0)\n"
        "\n"
        "analyze options:\n"
        "  --depth    N             3D: slice count      (default 1)\n"
        "  --atlas                  3D: input is one stacked file\n"
        "  --out      dir           report dir           (default report)\n"
        "  writes chN_spectrum.tga, chN_radial.csv, chN_temporal.csv (3D)\n"
        "\n"
        "examples:\n"
        "  noisebuilder gen --size 128 --out bn.tga                       1 ch blue noise\n"
        "  noisebuilder gen --size 128 --channels 4 --out bn4.tga         4 independent ch\n"
        "  noisebuilder gen --algo stbn --size 128 --depth 64 --out st.tga  blue over time\n"
        "  noisebuilder gen --algo stbn-swap --dist coshemi --size 64 --depth 32 --out ao.tga\n"
        "  noisebuilder analyze st.tga --depth 64 --out report\n");
}
