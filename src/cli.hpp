#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Parsed command line: "noisebuilder <command> --key value --flag ..."
struct Cli {
    std::string command;                                  // gen | analyze | help
    std::unordered_map<std::string, std::string> opts;   // "--size 128" -> opts["size"] = "128"
    std::vector<std::string> args;                       // plain args, e.g. file name
    std::string error;                                    // non-empty = parse failed

    bool has(const std::string& k) const { return opts.count(k) != 0; }
    std::string get(const std::string& k, const std::string& def) const;
    int getInt(const std::string& k, int def) const;
    float getFloat(const std::string& k, float def) const;
};

Cli parseCli(int argc, char** argv);
void printHelp();
