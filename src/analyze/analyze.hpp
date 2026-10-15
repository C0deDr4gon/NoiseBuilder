#pragma once
#include <string>
#include "image.hpp"

// Quality numbers. Power is normalized so white noise = 1.0 at every frequency.
struct ChannelReport {
    double lowFreq = 0;      // mean spatial power, 0 < radius <= size/8. Lower = more blue.
    double highFreq = 0;     // mean spatial power, radius > size/4. ~1+ for blue.
    double tLowFreq = -1;    // 3D only: mean temporal power, low freqs. -1 = not 3D.
    int histMin = 0, histMax = 0;   // byte histogram bin counts (256 bins)
    double seamX = 0, seamY = 0;    // wrap edge diff / inner diff. ~1.0 = seamless.
};

struct Report {
    ChannelReport ch[4];
    int channels = 0;
    double maxCrossCorr = 0;  // max |pearson| between any two channels.
                              // Note: swap moves whole pixel vectors -> this is fixed by init.
};

// Run all checks. outDir non-empty -> write spectrum TGA + CSV files there.
Report analyzeImage(const Image& img, const std::string& outDir);

void printReport(const Report& r, const Image& img);
