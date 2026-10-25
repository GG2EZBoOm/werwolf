#include "engine/core/Time.h"

#include <chrono>
#include <thread>

namespace eng {
namespace {
const auto g_start = std::chrono::steady_clock::now();
}

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

double processSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - g_start).count();
}

void sleepSeconds(double seconds) {
    if (seconds <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

} // namespace eng
