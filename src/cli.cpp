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
        "  --size     N             width = height, pot  (default 64)\n"
        "  --depth    N             time slices, pot     (default 1)\n"
        "  --channels 1|2|4         channels             (default 1)\n"
        "  --seed     N             rng seed             (default 1)\n"
        "  --sigma    F             energy sigma         (default 1.9)\n"
        "  --threads  N             0 = all cores        (default 0)\n"
        "  --out      file.tga      output file          (default noise.tga)\n"
        "  --atlas                  3D: one stacked file, not slices\n");
}
