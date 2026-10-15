#include "cli.hpp"
#include <cstdio>

int main(int argc, char** argv) {
    Cli cli = parseCli(argc, argv);
    if (!cli.error.empty()) {
        std::fprintf(stderr, "error: %s\n\n", cli.error.c_str());
        printHelp();
        return 1;
    }

    if (cli.command == "help") {
        printHelp();
        return 0;
    }

    // gen / analyze come in later steps
    std::fprintf(stderr, "error: unknown or not yet built command: %s\n", cli.command.c_str());
    return 1;
}
