#pragma once
#include <cmath>

namespace deadzone {
// This is an additional deadzone on SDK-normalized values, not raw calibration.
// 0 means exact passthrough, including diagonal values outside the unit circle.
inline void apply(float &x, float &y, int percent) {
    if (percent <= 0) return;
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    const float radius = std::sqrt(x * x + y * y);
    const float inner = percent / 100.0f;
    if (radius <= inner) { x = 0.0f; y = 0.0f; return; }
    const float output = (std::fmin(radius, 1.0f) - inner) / (1.0f - inner);
    const float scale = output / radius;
    x *= scale;
    y *= scale;
}
}
