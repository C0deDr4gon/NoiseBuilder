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
        "  --algo     vc|swap|stbn  generator            (default vc)\n"
        "             vc   = void and cluster, fast baseline\n"
        "             swap = energy swap optimizer, better (slices independent)\n"
        "             stbn = spatiotemporal: blue in space + blue over time\n"
        "  --size     N             width = height, pot  (default 64)\n"
        "  --depth    N             time slices, pot     (default 1)\n"
        "  --channels 1|2|4         channels             (default 1)\n"
        "  --seed     N             rng seed             (default 1)\n"
        "  --sigma    F             spatial sigma        (default vc 1.9, swap 2.1)\n"
        "  --threads  N             0 = all cores        (default 0)\n"
        "  --out      file.tga      output file          (default noise.tga)\n"
        "  --atlas                  3D: one stacked file, not slices\n"
        "  --report   dir           also run analyze, files to dir\n"
        "\n"
        "swap / stbn options:\n"
        "  --sigma-v  F             value sigma          (default 1.0)\n"
        "  --iters    N             swap tries per pixel (default 100)\n"
        "  --temp     F             anneal start temp    (default 0 = greedy)\n"
        "  --init     rand|vc       start pattern        (default rand)\n"
        "  --sigma-t  F             stbn time sigma      (default 2.1)\n"
        "  --time-weight F          stbn time vs space   (default 1.0)\n"
        "\n"
        "analyze options:\n"
        "  --depth    N             3D: slice count      (default 1)\n"
        "  --atlas                  3D: input is one stacked file\n"
        "  --out      dir           report dir           (default report)\n"
        "  writes chN_spectrum.tga, chN_radial.csv, chN_temporal.csv (3D)\n");
}
