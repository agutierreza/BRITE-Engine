#pragma once

#include "RenderPass.hpp"

#include <algorithm>
#include <cmath>

// THE PLANAR REFLECTION'S BLUR (RenderPass.hpp, ReflectionBlurSettings), planned on the CPU: how far it spreads each
// way in the reflection's own texels, and the two passes that gather it -- across the screen, then down it -- each a
// Gaussian read from one level of the reflection's mipmaps. The backend draws exactly this plan; a caller and a test
// can reach it.
//
// WHY LEVELS. A pass gathers Taps texels either side of each pixel, 3 standard deviations of its Gaussian, so a spread
// of s texels costs 6 s + 1 reads a pixel. A long streak on a large reflection -- 0.12 of a height of 1080, 130 texels
// -- would cost 780. So a pass whose spread is beyond MostLevelSpread texels reads a coarser level of what it blurs,
// where each texel is 2^L of the reflection's: the spread there is no more than MostLevelSpread, and the pass no more
// than MostTaps reads either side.
//
// WHAT A LEVEL ADDS. A level L > 0 has already spread the reflection both ways before the pass reads it, each way, in
// the reflection's texels, with n = 2^L:
//   - its mipmap averaged a box of n texels: a variance of (n^2 - 1) / 12;
//   - a pass's pixels, at the reflection's texel centres, read it between its own texels, n apart, bilinearly: a tent
//     n wide each side, met at the half-texels u = k + 1/2. Its weights 1 - u/n sum, both sides, to 2 (n - n/2) = n,
//     and its second moment is 2 (sum u^2 - sum u^3 / n) over k = 0..n-1, with sum u^2 = n (4n^2 - 1) / 12 and
//     sum u^3 = n^2 (2n^2 - 1) / 8: 2n ((8n^2 - 2) - (6n^2 - 3)) / 24 = n (2n^2 + 1) / 12, so a variance of
//     (2n^2 + 1) / 12.
// Together n^2 / 4: 1 at level 1, 4 at level 2. The reflection itself, read at its texel centres, adds nothing. Both
// passes add their level's to both axes, so each pass gathers only what its own axis still lacks -- its spread squared
// less what the two levels add -- and the stretch stays what was asked when the long pass reads a coarse level and the
// short one does not.
namespace BRITE::ReflectionBlur {

// The settings' holds (ReflectionBlurSettings).
inline constexpr float MostSoftness = 0.01f;
inline constexpr float LeastStretch = 1.0f;
inline constexpr float MostStretch = 12.0f;
// The least spread across, in the reflection's own texels, of a blur that is on: a reflection drawn small reads soft,
// not as blocks.
inline constexpr float LeastSpread = 1.0f;
// The most spread a pass gathers at the level it reads, in that level's texels, and so the most reads either side.
inline constexpr float MostLevelSpread = 16.0f;
inline constexpr int MostTaps = 48;

// One pass of the blur, along its axis: the level of its source it reads, the Gaussian's standard deviation in that
// level's texels (Spread), the reads either side of the pixel (Taps, one a texel of the level), and one texel of the
// level in the uv of the axis it runs along (Step). A pass with no taps copies its level.
struct Pass {
    int Level = 0;
    float Spread = 0.0f;
    int Taps = 0;
    float Step = 0.0f;
};

// The blur of a reflection: whether it blurs at all; the spread asked across and down, in the reflection's texels; and
// the two passes that draw it.
struct Plan {
    bool Blurs = false;
    float Across = 0.0f;
    float Down = 0.0f;
    Pass AcrossPass;
    Pass DownPass;
};

// The variance a level adds to every axis of a pass that reads it, in the reflection's texels squared (above): 0 for
// the reflection itself.
inline float LevelVariance(int level) {
    if (level <= 0)
        return 0.0f;
    const float n = std::ldexp(1.0f, level);
    return n * n / 4.0f;
}

// The finest level at which `spread` texels of the reflection is no more than MostLevelSpread of the level's own, and
// no coarser than a reflection `smallest` texels on its shorter side holds.
inline int LevelFor(float spread, int smallest) {
    int level = 0;
    while (spread > MostLevelSpread * std::ldexp(1.0f, level) && (2 << level) <= smallest)
        ++level;
    return level;
}

// The plan for a reflection `width` x `height` texels under `settings`.
inline Plan PlanFor(const ReflectionBlurSettings& settings, int width, int height) {
    Plan plan;
    const float softness = std::min(settings.Softness, MostSoftness);
    if (!settings.Enabled || !(softness > 0.0f) || width <= 0 || height <= 0)
        return plan;
    const float stretch =
        settings.Stretch >= LeastStretch ? std::min(settings.Stretch, MostStretch) : LeastStretch; // NaN too
    plan.Blurs = true;
    plan.Across = std::max(softness * static_cast<float>(height), LeastSpread);
    plan.Down = plan.Across * stretch;

    const int smallest = std::min(width, height);
    const int acrossLevel = LevelFor(plan.Across, smallest);
    const int downLevel = LevelFor(plan.Down, smallest);
    const float levels = LevelVariance(acrossLevel) + LevelVariance(downLevel);
    auto pass = [&](float spread, int level, int size) {
        Pass p;
        p.Level = level;
        const int levelSize = std::max(1, size >> level);
        p.Step = 1.0f / static_cast<float>(levelSize);
        // What this axis still lacks once both levels have spread it, in the reflection's texels, then the level's.
        const float own = std::sqrt(std::max(spread * spread - levels, 0.0f));
        p.Spread = own * static_cast<float>(levelSize) / static_cast<float>(size);
        p.Taps = std::min(static_cast<int>(std::ceil(3.0f * p.Spread)), MostTaps);
        return p;
    };
    plan.AcrossPass = pass(plan.Across, acrossLevel, width);
    plan.DownPass = pass(plan.Down, downLevel, height);
    return plan;
}

} // namespace BRITE::ReflectionBlur
