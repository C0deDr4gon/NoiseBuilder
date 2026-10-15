#include <chrono>
#include <cstdio>
#include "analyze/analyze.hpp"
#include "cli.hpp"
#include "gen/swap.hpp"
#include "gen/void_cluster.hpp"
#include "image.hpp"
#include "io/tga.hpp"
#include "thread_pool.hpp"

static int fail(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    return 1;
}

static int cmdGen(const Cli& cli) {
    std::string algo = cli.get("algo", "vc");
    int size     = cli.getInt("size", 64);
    int depth    = cli.getInt("depth", 1);
    int channels = cli.getInt("channels", 1);
    int seed     = cli.getInt("seed", 1);
    int threads  = cli.getInt("threads", 0);
    float sigma  = cli.getFloat("sigma", algo == "vc" ? 1.9f : 2.1f);
    std::string out = cli.get("out", "noise.tga");
    bool atlas   = cli.has("atlas");

    if (!isPow2(size) || size < 4) return fail("--size must be power of two >= 4");
    if (!isPow2(depth))            return fail("--depth must be power of two");
    if (channels != 1 && channels != 2 && channels != 4) return fail("--channels must be 1, 2 or 4");
    if (sigma <= 0.0f)             return fail("--sigma must be > 0");
    if (algo != "vc" && algo != "swap" && algo != "stbn") return fail("--algo must be vc, swap or stbn");
    if (algo == "stbn" && depth < 2) return fail("stbn needs --depth >= 2 (2D: use swap)");

    std::printf("gen %s  %dx%dx%d  ch %d  seed %d  sigma %.2f  simd %s\n",
                algo.c_str(), size, size, depth, channels, seed, sigma, simd::name());
    auto t0 = std::chrono::steady_clock::now();

    Image img(size, size, depth, channels);
    if (algo == "vc") {
        // 2D only. Each slice + channel = own texture (own rng stream).
        int jobs = depth * channels;
        parallelFor(jobs, threads, [&](int j) {
            int z = j / channels, ch = j % channels;
            VcParams p;
            p.w = p.h = size;
            p.sigma = sigma;
            p.seed = (uint64_t)seed;
            p.stream = (uint64_t)j;
            voidCluster(p, &img.at(0, 0, z, ch));
        });
    } else {
        // swap / stbn: all channels optimized together (vector value).
        std::string init = cli.get("init", "rand");
        if (init != "rand" && init != "vc") return fail("--init must be rand or vc");
        SwapParams p;
        p.size = size;
        p.channels = channels;
        p.sigmaS = sigma;
        p.sigmaT = cli.getFloat("sigma-t", 2.1f);
        p.timeWeight = cli.getFloat("time-weight", 1.0f);
        p.sigmaV = cli.getFloat("sigma-v", 1.0f);
        p.iters = cli.getInt("iters", 100);
        p.temp = cli.getFloat("temp", 0.0f);
        p.initVc = init == "vc";
        p.seed = (uint64_t)seed;
        if (p.iters < 1) return fail("--iters must be >= 1");
        if (p.sigmaT <= 0.0f || p.sigmaV <= 0.0f) return fail("--sigma-t / --sigma-v must be > 0");
        if (p.timeWeight < 0.0f) return fail("--time-weight must be >= 0");

        float* planes[4];
        if (algo == "stbn") {
            // whole volume at once, threads inside optimizer
            p.depth = depth;
            p.threads = threads;
            p.progress = true;
            for (int c = 0; c < channels; ++c) planes[c] = img.plane(c);
            swapOptimize(p, planes);
        } else {
            // swap: slices independent. one slice -> threads inside, many -> thread per slice.
            p.threads = depth == 1 ? threads : 1;
            p.progress = depth == 1;
            parallelFor(depth, depth == 1 ? 1 : threads, [&](int z) {
                SwapParams pz = p;
                pz.stream = (uint64_t)z;
                float* pl[4];
                for (int c = 0; c < channels; ++c) pl[c] = &img.at(0, 0, z, c);
                swapOptimize(pz, pl);
            });
        }
    }

    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("done in %.2fs\n", sec);

    std::string err;
    bool ok = atlas ? writeTgaAtlas(out, img, err) : writeTgaSlices(out, img, err);
    if (!ok) return fail(err);
    std::printf("wrote %s%s\n", out.c_str(), depth > 1 && !atlas ? " (slices _000..)" : "");

    if (cli.has("report")) {
        std::string dir = cli.get("report", "report");
        if (dir == "1") dir = "report";
        printReport(analyzeImage(img, dir), img);
    }
    return 0;
}

// Load 2D file, slices (name_000.tga ..) or atlas into one volume.
static bool loadVolume(const std::string& path, int depth, bool atlas, Image& vol, std::string& err) {
    if (depth == 1 || atlas) {
        Image a;
        if (!readTga(path, a, err)) return false;
        if (a.h % depth) { err = "atlas height not divisible by --depth"; return false; }
        int h = a.h / depth;
        vol = Image(a.w, h, depth, a.c);
        for (int c = 0; c < a.c; ++c)
            for (int z = 0; z < depth; ++z)
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < a.w; ++x)
                        vol.at(x, y, z, c) = a.at(x, z * h + y, 0, c);
        return true;
    }
    for (int z = 0; z < depth; ++z) {
        Image s;
        if (!readTga(slicePath(path, z), s, err)) return false;
        if (z == 0) vol = Image(s.w, s.h, depth, s.c);
        if (s.w != vol.w || s.h != vol.h || s.c != vol.c) { err = "slice size mismatch"; return false; }
        for (int c = 0; c < s.c; ++c)
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                    vol.at(x, y, z, c) = s.at(x, y, 0, c);
    }
    return true;
}

static int cmdAnalyze(const Cli& cli) {
    if (cli.args.empty()) return fail("analyze: need file.tga");
    int depth = cli.getInt("depth", 1);
    if (!isPow2(depth)) return fail("--depth must be power of two");
    std::string dir = cli.get("out", "report");

    Image vol;
    std::string err;
    if (!loadVolume(cli.args[0], depth, cli.has("atlas"), vol, err)) return fail(err);
    printReport(analyzeImage(vol, dir), vol);
    std::printf("report files in %s/\n", dir.c_str());
    return 0;
}

int main(int argc, char** argv) {
    Cli cli = parseCli(argc, argv);
    if (!cli.error.empty()) {
        std::fprintf(stderr, "error: %s\n\n", cli.error.c_str());
        printHelp();
        return 1;
    }

    if (cli.command == "help") { printHelp(); return 0; }
    if (cli.command == "gen")     return cmdGen(cli);
    if (cli.command == "analyze") return cmdAnalyze(cli);
    return fail("unknown command: " + cli.command);
}
