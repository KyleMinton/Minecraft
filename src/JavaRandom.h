#pragma once

#include <cmath>
#include <cstdint>

// java.util.Random, bit for bit, so anything Minecraft derives from a seeded
// Random (star positions, HUD heart shake) comes out exactly the same.
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed) : state(((uint64_t)seed ^ MULTIPLIER) & MASK) {}

    void SetSeed(int64_t seed) { state = ((uint64_t)seed ^ MULTIPLIER) & MASK; haveNextGaussian = false; }

    int NextInt() { return Next(32); }
    int64_t NextLong() { return ((int64_t)Next(32) << 32) + Next(32); }
    bool NextBoolean() { return Next(1) != 0; }
    float NextFloat() { return Next(24) / (float)(1 << 24); }

    double NextDouble() {
        return (double)(((int64_t)Next(26) << 27) + Next(27)) * (1.0 / (double)(1LL << 53));
    }

    double NextGaussian() {
        if (haveNextGaussian) {
            haveNextGaussian = false;
            return nextGaussian;
        }
        double v1, v2, s;
        do {
            v1 = 2.0 * NextDouble() - 1.0;
            v2 = 2.0 * NextDouble() - 1.0;
            s = v1 * v1 + v2 * v2;
        } while (s >= 1.0 || s == 0.0);
        double multiplier = std::sqrt(-2.0 * std::log(s) / s);
        nextGaussian = v2 * multiplier;
        haveNextGaussian = true;
        return v1 * multiplier;
    }

    int NextInt(int bound) {
        if ((bound & -bound) == bound) return (int)(((int64_t)bound * Next(31)) >> 31); // power of two
        int bits, value;
        do {
            bits = Next(31);
            value = bits % bound;
        } while (bits - value + (bound - 1) < 0);
        return value;
    }

private:
    static constexpr uint64_t MULTIPLIER = 0x5DEECE66DULL;
    static constexpr uint64_t MASK = (1ULL << 48) - 1;
    uint64_t state;
    double nextGaussian = 0.0;
    bool haveNextGaussian = false;

    int Next(int bits) {
        state = (state * MULTIPLIER + 0xBULL) & MASK;
        return (int)(int64_t)(state >> (48 - bits));
    }
};
