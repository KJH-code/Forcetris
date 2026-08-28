#include "gfx.hpp"

#include <algorithm>
#include <filesystem>
#include <map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace forcetris {
namespace gui {
namespace gfx {

namespace {

SDL_Renderer* the_renderer = nullptr;
std::string the_root;
// nullptr entries are remembered too: a missing file is asked of the disk
// once, not once per frame.
std::map<std::string, SDL_Texture*> cache;

SDL_Texture* load (const std::string& path) {
	// Through SDL_RWops rather than fopen, because on Android the "file"
	// may live inside the APK where stdio cannot see it. (The extractor
	// usually unpacks gfx/ to real storage first, but this way the loader
	// does not depend on that.)
	SDL_RWops* file = SDL_RWFromFile(path.c_str(), "rb");
	if (file == nullptr) {
		return nullptr;
	}
	const Sint64 size = SDL_RWsize(file);
	if (size <= 0) {
		SDL_RWclose(file);
		return nullptr;
	}
	std::string bytes(static_cast<size_t>(size), '\0');
	SDL_RWread(file, bytes.data(), 1, bytes.size());
	SDL_RWclose(file);
	int w = 0;
	int h = 0;
	int channels = 0;
	unsigned char* pixels = stbi_load_from_memory(
		reinterpret_cast<const unsigned char*>(bytes.data()),
		static_cast<int>(bytes.size()), &w, &h, &channels, 4);
	if (pixels == nullptr) {
		return nullptr;
	}
	SDL_Texture* texture = SDL_CreateTexture(the_renderer,
		SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
	if (texture != nullptr) {
		SDL_UpdateTexture(texture, nullptr, pixels, w * 4);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	stbi_image_free(pixels);
	return texture;
}

} // namespace

void init (SDL_Renderer* renderer, const std::string& root) {
	the_renderer = renderer;
	the_root = root;
}

void shutdown () {
	for (auto& [name, texture] : cache) {
		if (texture != nullptr) {
			SDL_DestroyTexture(texture);
		}
	}
	cache.clear();
	the_renderer = nullptr;
}

SDL_Texture* get (const char* name) {
	if (the_renderer == nullptr) {
		return nullptr;
	}
	const auto found = cache.find(name);
	if (found != cache.end()) {
		return found->second;
	}
	SDL_Texture* texture = load(
		(std::filesystem::path(the_root) / "gfx" / (std::string(name)
			+ ".png")).string());
	cache[name] = texture;
	return texture;
}

bool draw (const char* name, const SDL_Rect& dst, Uint8 alpha,
	SDL_Color tint) {
	SDL_Texture* texture = get(name);
	if (texture == nullptr) {
		return false;
	}
	SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
	SDL_SetTextureAlphaMod(texture, alpha);
	SDL_RenderCopy(the_renderer, texture, nullptr, &dst);
	SDL_SetTextureColorMod(texture, 255, 255, 255);
	SDL_SetTextureAlphaMod(texture, 255);
	return true;
}

bool nine (const char* name, const SDL_Rect& dst, int border, Uint8 alpha) {
	SDL_Texture* texture = get(name);
	if (texture == nullptr) {
		return false;
	}
	int tw = 0;
	int th = 0;
	SDL_QueryTexture(texture, nullptr, nullptr, &tw, &th);
	// A destination too small for two borders shrinks them to fit rather
	// than letting the slices overlap.
	const int b = border;
	const int ob = std::max(1, std::min({b, dst.w / 2, dst.h / 2}));
	SDL_SetTextureAlphaMod(texture, alpha);
	const int sx[4] = {0, b, tw - b, tw};
	const int sy[4] = {0, b, th - b, th};
	const int dx[4] = {dst.x, dst.x + ob, dst.x + dst.w - ob,
		dst.x + dst.w};
	const int dy[4] = {dst.y, dst.y + ob, dst.y + dst.h - ob,
		dst.y + dst.h};
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			SDL_Rect src{sx[col], sy[row], sx[col + 1] - sx[col],
				sy[row + 1] - sy[row]};
			SDL_Rect out{dx[col], dy[row], dx[col + 1] - dx[col],
				dy[row + 1] - dy[row]};
			if (src.w > 0 && src.h > 0 && out.w > 0 && out.h > 0) {
				SDL_RenderCopy(the_renderer, texture, &src, &out);
			}
		}
	}
	SDL_SetTextureAlphaMod(texture, 255);
	return true;
}

} // namespace gfx
} // namespace gui
} // namespace forcetris
