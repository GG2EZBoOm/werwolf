#pragma once

namespace eng {

// Monotone Zeit in Sekunden (beliebiger Nullpunkt).
double nowSeconds();
// Sekunden seit Programmstart.
double processSeconds();
void sleepSeconds(double seconds);

} // namespace eng
