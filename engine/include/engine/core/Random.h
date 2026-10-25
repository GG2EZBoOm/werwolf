#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace eng {

class Rng {
public:
    explicit Rng(uint64_t seed = 0x5eed5eedULL) : m_engine(seed) {}

    void seed(uint64_t s) { m_engine.seed(s); }
    uint64_t next64() { return m_engine(); }

    // Ganzzahl im geschlossenen Intervall [lo, hi].
    int range(int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(m_engine);
    }
    float uniform() {
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        return d(m_engine);
    }
    float uniform(float lo, float hi) { return lo + (hi - lo) * uniform(); }
    bool chance(float p) { return uniform() < p; }

    template <class T>
    void shuffle(std::vector<T>& v) {
        std::shuffle(v.begin(), v.end(), m_engine);
    }
    template <class T>
    const T& pick(const std::vector<T>& v) {
        return v[static_cast<size_t>(range(0, static_cast<int>(v.size()) - 1))];
    }

private:
    std::mt19937_64 m_engine;
};

inline uint64_t randomSeed() {
    std::random_device rd;
    uint64_t s = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    s ^= static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return s;
}

} // namespace eng
