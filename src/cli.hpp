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
    mutable std::string badValue;                        // set by getInt/getFloat on bad number

    bool has(const std::string& k) const { return opts.count(k) != 0; }
    std::string get(const std::string& k, const std::string& def) const;
    int getInt(const std::string& k, int def) const;
    float getFloat(const std::string& k, float def) const;

    // First option not in `known`, or "" if all ok.
    std::string unknownOption(const std::vector<std::string>& known) const;
};

Cli parseCli(int argc, char** argv);
void printHelp();

constexpr const char* kVersion = "0.1.0";
