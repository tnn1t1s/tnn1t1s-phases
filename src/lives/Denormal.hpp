#pragma once

// The denormal floor, on its own so headers that must not pull in <rack.hpp>
// can still use it. CLIPS had drifted to a bare 1e-20f literal while TAPES
// used the named constant, which is the first symptom of a shared idea living
// in two places.

namespace Lives {

static constexpr float kDenormalFloor = 1e-20f;

// Flush denormals AND NaN. `abs(x) < eps` is false for NaN, so the plain
// comparison leaves a poisoned filter poisoned; the negated form sends NaN
// down the zero branch.
inline void flush(float& x) {
    if (!(x > kDenormalFloor) && !(x < -kDenormalFloor)) x = 0.f;
}

}  // namespace Lives
