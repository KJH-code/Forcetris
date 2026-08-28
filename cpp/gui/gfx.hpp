// The game's images, loaded once and drawn anywhere.
//
// Every asset lives in <root>/gfx as a PNG that tools/make_gfx.py made -
// the art is versioned as pictures and as the code that painted them.
// This module is deliberately forgiving: a missing file loads as nullptr,
// is remembered as missing, and every caller falls back to the old
// procedural drawing - so the game still runs from a checkout with no
// gfx directory at all, and the smoke harnesses prove that it does.
#pragma once

#include <string>

#include <SDL.h>

namespace forcetris {
namespace gui {
namespace gfx {

// Point the cache at the renderer and the game root once, at startup.
void init (SDL_Renderer* renderer, const std::string& root);
void shutdown ();

// The texture for gfx/<name>.png, or nullptr when it does not exist.
// Textures are owned by the cache; never destroy one.
SDL_Texture* get (const char* name);

// Draw an image scaled into dst. Returns false (drawing nothing) when the
// asset is missing, so callers can fall back in one line.
bool draw (const char* name, const SDL_Rect& dst, Uint8 alpha = 255,
	SDL_Color tint = {255, 255, 255, 255});

// Draw a nine-slice frame: corners stay square, edges stretch along their
// length, the middle fills. `border` is in the *source* image's pixels.
bool nine (const char* name, const SDL_Rect& dst, int border,
	Uint8 alpha = 255);

} // namespace gfx
} // namespace gui
} // namespace forcetris
