#include "cli.hpp"
#include "simd.hpp"
#include <cstdio>
#include <cstdlib>

std::string Cli::get(const std::string& k, const std::string& def) const {
    auto it = opts.find(k);
    return it == opts.end() ? def : it->second;
}

int Cli::getInt(const std::string& k, int def) const {
    auto it = opts.find(k);
    return it == opts.end() ? def : std::atoi(it->second.c_str());
}

float Cli::getFloat(const std::string& k, float def) const {
    auto it = opts.find(k);
    return it == opts.end() ? def : (float)std::atof(it->second.c_str());
}

Cli parseCli(int argc, char** argv) {
    Cli cli;
    if (argc < 2) { cli.command = "help"; return cli; }

    cli.command = argv[1];
    if (cli.command == "-h" || cli.command == "--help") cli.command = "help";

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
    std::printf("noisebuilder - blue noise texture generator (simd: %s, %d wide)\n\n",
                simd::name(), simd::W);
    std::printf(
        "usage:\n"
        "  noisebuilder gen     [options]   make noise texture\n"
        "  noisebuilder analyze <file.tga>  check noise quality\n"
        "  noisebuilder help                this text\n"
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
        "  writes chN_spectrum.tga, chN_radial.csv, chN_temporal.csv (3D)\n");
}
