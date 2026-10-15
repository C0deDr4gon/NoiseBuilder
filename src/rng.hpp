#pragma once
#include <cstdint>

// PCG32. Small, fast, same output on every platform.
struct Rng {
    uint64_t state = 0;
    uint64_t inc = 1;

    explicit Rng(uint64_t seed, uint64_t stream = 0) {
        inc = (stream << 1u) | 1u;
        next();
        state += seed;
        next();
    }

    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xs >> rot) | (xs << ((32u - rot) & 31u));
    }

    // [0, n) without modulo bias
    uint32_t below(uint32_t n) {
        uint32_t threshold = (0u - n) % n;
        for (;;) {
            uint32_t r = next();
            if (r >= threshold) return r % n;
        }
    }

    // [0, 1)
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
};
