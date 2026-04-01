#ifndef SLOPE_COLOURING_H
#define SLOPE_COLOURING_H

#include <d2d1.h>
#include <algorithm>
#include <cmath>

// Slope-based colouring helpers for progressive track rendering.

namespace gpxlister_slope {

// Adaptive distance window for slope-based colouring.
// Colours are updated once per window, where the window length can vary with slope.
// This reduces flicker on dense/noisy GPX traces while keeping detail on steep sections.
static const double kSlopeColourSampleMetresMin = 25.0; // Minimum sample length in metres.
static const double kSlopeColourSampleMetresMax = 100.0; // Maximum sample length in metres.
static const double kSlopeColourAdaptiveGradeScale = 4.0; // Grade scale in percent controlling adaptivity.
static const double kSlopeColourGradeDeadband = 1.0; // Grades within this band (percent) are treated as flat.

static inline double ApplyGradeDeadband(double gradePercent) {
    if (std::fabs(gradePercent) < kSlopeColourGradeDeadband) {
        return 0.0;
    }
    return gradePercent;
}

static inline double AdaptiveSampleMetresFromGrade(double gradePercent) {
    const double g = std::fabs(ApplyGradeDeadband(gradePercent));
    const double denom = 1.0 + (g / kSlopeColourAdaptiveGradeScale);
    double metres = kSlopeColourSampleMetresMax / denom;
    if (metres < kSlopeColourSampleMetresMin) metres = kSlopeColourSampleMetresMin;
    if (metres > kSlopeColourSampleMetresMax) metres = kSlopeColourSampleMetresMax;
    return metres;
}

static inline float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline double ClampGradePercent(double gradePercent) {
    const double kMinGradePercent = -12.0; // Lower bound for slope colour mapping in percent.
    const double kMaxGradePercent = 12.0;  // Upper bound for slope colour mapping in percent.
    if (gradePercent < kMinGradePercent) return kMinGradePercent;
    if (gradePercent > kMaxGradePercent) return kMaxGradePercent;
    return gradePercent;
}

static inline D2D1_COLOR_F LerpColour(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    t = Clamp01(t);
    D2D1_COLOR_F out{};
    out.r = a.r + (b.r - a.r) * t;
    out.g = a.g + (b.g - a.g) * t;
    out.b = a.b + (b.b - a.b) * t;
    out.a = a.a + (b.a - a.a) * t;
    return out;
}

static inline D2D1_COLOR_F SlopeToColour(double gradePercent) {
    // Mapping targets consistent with the existing elevation profile palette:
    // Downhill: blue, flat: green, uphill: red.
    const D2D1_COLOR_F downhill = D2D1::ColorF(0.0f, 0.4f, 0.8f, 1.0f);
    const D2D1_COLOR_F flat = D2D1::ColorF(0.0f, 0.7f, 0.0f, 1.0f);
    const D2D1_COLOR_F uphill = D2D1::ColorF(0.8f, 0.0f, 0.0f, 1.0f);

    const double g = ClampGradePercent(gradePercent);

    if (g < 0.0) {
        const float t = (float)((g - (-12.0)) / (0.0 - (-12.0)));
        return LerpColour(downhill, flat, t);
    }

    {
        const float t = (float)(g / 12.0);
        return LerpColour(flat, uphill, t);
    }
}

static inline D2D1_COLOR_F BlendTrackColour(const D2D1_COLOR_F& baseColour, double gradePercent) {
    const float kSlopeBlendWeight = 0.65f; // Weight of slope colour overlay in the final track colour (0..1).
    const double gb = ApplyGradeDeadband(gradePercent);
    const D2D1_COLOR_F slopeColour = SlopeToColour(gb);
    return LerpColour(baseColour, slopeColour, kSlopeBlendWeight);
}

static inline double SafeGradePercent(double deltaElevationMetres, double segmentDistanceMetres) {
    const double kMinSegmentDistanceMetres = 1e-3; // Lower bound to avoid division by zero and noise.
    if (segmentDistanceMetres <= kMinSegmentDistanceMetres) {
        return 0.0;
    }
    return (deltaElevationMetres / segmentDistanceMetres) * 100.0;
}

} // namespace gpxlister_slope

#endif
