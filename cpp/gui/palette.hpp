// The one place a colour is named.
//
// The palette used to be forked five ways - tools/make_gfx.py's constants,
// apply_theme's function-locals, kFormColors, kFireRamp, and something near
// two hundred literals inline in the draw code - and the forks had drifted.
// The generator's ember was (255,122,46) and the chrome's accent was
// (255,138,58): two different oranges, sitting next to each other on every
// screen, neither of them wrong on purpose.
//
// tools/make_gfx.py holds the same numbers on the Python side, because it
// bakes them into PNGs rather than reading them. The two halves are a pair:
// a change here wants the same change there and a rerun of the generator.
#pragma once

#include <SDL.h>

#include "imgui.h"

namespace forcetris {
namespace gui {
namespace ink {

struct Hue {
	Uint8 r, g, b;
};

// The grounds: soot, and the darker well a control sits in.
constexpr Hue kSoot{22, 16, 12};
constexpr Hue kPanel{25, 20, 16};
constexpr Hue kWell{42, 31, 22};
constexpr Hue kEdge{86, 76, 64};
constexpr Hue kEdgeLit{140, 124, 104};

// The letters.
constexpr Hue kInk{244, 237, 228};
constexpr Hue kMuted{157, 140, 120};
constexpr Hue kSteel{143, 163, 184};

// The fire, coolest first. Ember is the accent the whole game is keyed to;
// Hot is the brighter mid a rim or a bar climbs to; Gold belongs to
// Overdrive and to anything won.
constexpr Hue kEmberDeep{196, 74, 24};
constexpr Hue kEmber{255, 122, 46};
constexpr Hue kEmberHot{255, 176, 60};
constexpr Hue kGold{255, 214, 94};
constexpr Hue kWhiteHot{255, 246, 232};

constexpr ImU32 col (const Hue& hue, int alpha = 255) {
	return IM_COL32(hue.r, hue.g, hue.b, alpha);
}

constexpr ImVec4 vec (const Hue& hue, float alpha = 1.f) {
	return ImVec4(hue.r / 255.f, hue.g / 255.f, hue.b / 255.f, alpha);
}

constexpr SDL_Color sdl (const Hue& hue, Uint8 alpha = 255) {
	return SDL_Color{hue.r, hue.g, hue.b, alpha};
}

} // namespace ink
} // namespace gui
} // namespace forcetris
