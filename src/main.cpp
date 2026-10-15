#include <chrono>
#include <cstdio>
#include "cli.hpp"
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
    float sigma  = cli.getFloat("sigma", 1.9f);
    std::string out = cli.get("out", "noise.tga");
    bool atlas   = cli.has("atlas");

    if (!isPow2(size) || size < 4) return fail("--size must be power of two >= 4");
    if (!isPow2(depth))            return fail("--depth must be power of two");
    if (channels != 1 && channels != 2 && channels != 4) return fail("--channels must be 1, 2 or 4");
    if (sigma <= 0.0f)             return fail("--sigma must be > 0");
    if (algo != "vc")              return fail("--algo " + algo + " not built yet (have: vc)");

    std::printf("gen %s  %dx%dx%d  ch %d  seed %d  sigma %.2f  simd %s\n",
                algo.c_str(), size, size, depth, channels, seed, sigma, simd::name());
    auto t0 = std::chrono::steady_clock::now();

    // vc = 2D only. Each slice + channel = own texture (own rng stream).
    Image img(size, size, depth, channels);
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

    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("done in %.2fs\n", sec);

    std::string err;
    bool ok = atlas ? writeTgaAtlas(out, img, err) : writeTgaSlices(out, img, err);
    if (!ok) return fail(err);
    std::printf("wrote %s%s\n", out.c_str(), depth > 1 && !atlas ? " (slices _000..)" : "");
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
    if (cli.command == "gen")  return cmdGen(cli);

    // analyze comes in step 5
    return fail("unknown or not yet built command: " + cli.command);
}
