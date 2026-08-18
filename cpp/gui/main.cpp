// The Forcetris GUI: the graded sim with a face on it.
//
// The game inside the window is cpp/src/sim.cpp - the code the trace harness
// proves agrees with the Python engine move for move - fed from the keyboard
// instead of a script. This file only draws what the sim reports and routes
// what the player asks: menus and settings are Dear ImGui, the board is
// plain SDL rectangles, and every screen is mouse-first.
//
// FORCETRIS_SMOKE=<frames> runs the game headlessly for that many frames
// with scripted-random input and exits, which is how the build is tested on
// machines with no display.
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "config.hpp"
#include "session.hpp"
#include "stats.hpp"

namespace forcetris {
namespace gui {
namespace {

// The board's place on screen. The stat panels anchor to its right edge, so
// their saved positions survive a window resize.
constexpr int kCell = 26;
constexpr int kBoardX = 300;
constexpr int kBoardY = 48;
constexpr int kBoardW = kWidth * kCell;
constexpr int kBoardH = kHeight * kCell;

// One colour per form, matching the Python game's palette closely enough
// that a player moving between the two is not surprised.
const SDL_Color kFormColors[8] = {
	{65, 198, 224, 255},   // I
	{242, 201, 76, 255},   // O
	{155, 81, 224, 255},   // T
	{111, 207, 87, 255},   // S
	{235, 87, 87, 255},    // Z
	{62, 123, 224, 255},   // J
	{242, 153, 74, 255},   // L
	{122, 122, 122, 255},  // garbage
};

enum class Screen { Menu, Game, Over };

struct App {
	SDL_Window* window = nullptr;
	SDL_Renderer* renderer = nullptr;
	Config config;
	std::string config_file;
	std::optional<Session> session;
	Screen screen = Screen::Menu;
	bool paused = false;
	bool editing = false;        // The stat layout editor is live.
	bool show_settings = false;
	bool place_panels = false;   // Push saved positions into ImGui this frame.
	std::mt19937 seeds{std::random_device{}()};
	bool quit = false;
};

void start_game (App& app) {
	app.session.emplace(app.config.sim(), app.seeds());
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
}

// --- Input. ----------------------------------------------------------------

std::optional<Key> key_for (SDL_Scancode code) {
	switch (code) {
		case SDL_SCANCODE_LEFT: return Key::Left;
		case SDL_SCANCODE_RIGHT: return Key::Right;
		case SDL_SCANCODE_DOWN: return Key::Soft;
		case SDL_SCANCODE_SPACE: return Key::Hard;
		case SDL_SCANCODE_LSHIFT:
		case SDL_SCANCODE_C: return Key::Hold;
		case SDL_SCANCODE_Z: return Key::Ccw;
		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_X: return Key::Cw;
		case SDL_SCANCODE_A: return Key::Flip;
		default: return std::nullopt;
	}
}

void handle_event (App& app, const SDL_Event& event) {
	ImGui_ImplSDL2_ProcessEvent(&event);
	if (event.type == SDL_QUIT) {
		app.quit = true;
		return;
	}
	if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
		return;
	}
	const bool down = event.type == SDL_KEYDOWN;
	if (down && event.key.repeat) {
		// The sim runs its own DAS; the OS keyboard repeat is not part of it.
		return;
	}
	if (down && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
		if (app.screen == Screen::Game) {
			if (app.editing) {
				app.editing = false;
			} else {
				app.paused = !app.paused;
			}
		}
		return;
	}
	if (app.screen != Screen::Game || app.paused || app.editing
		|| ImGui::GetIO().WantCaptureKeyboard) {
		return;
	}
	if (const auto key = key_for(event.key.keysym.scancode)) {
		app.session->key(*key, down);
	}
}

// --- The board and its trimmings, in plain rectangles. ---------------------

void fill (SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	const SDL_Rect rect{x, y, w, h};
	SDL_RenderFillRect(renderer, &rect);
}

void draw_cell (SDL_Renderer* renderer, int px, int py, SDL_Color c, int size = kCell) {
	fill(renderer, px + 1, py + 1, size - 2, size - 2, c);
}

// A piece drawn on its own, for the hold box and the queue previews.
void draw_preview (SDL_Renderer* renderer, int form, int left, int top, int size) {
	if (form < 0 || form > 6) {
		return;
	}
	const Piece piece{form, 0, 0, 0};
	for (const Offset cell : cells_of(piece)) {
		draw_cell(renderer, left + (cell.x + 1) * size, top + (cell.y + 1) * size,
			kFormColors[form], size);
	}
}

void draw_board (App& app) {
	SDL_Renderer* renderer = app.renderer;
	const Session& session = *app.session;
	const Sim& sim = session.sim();

	fill(renderer, kBoardX - 3, kBoardY - 3, kBoardW + 6, kBoardH + 6, {32, 40, 53, 255});
	fill(renderer, kBoardX, kBoardY, kBoardW, kBoardH, {14, 18, 24, 255});
	SDL_SetRenderDrawColor(renderer, 26, 33, 44, 255);
	for (int x = 1; x < kWidth; ++x) {
		SDL_RenderDrawLine(renderer, kBoardX + x * kCell, kBoardY,
			kBoardX + x * kCell, kBoardY + kBoardH - 1);
	}
	for (int y = 1; y < kHeight; ++y) {
		SDL_RenderDrawLine(renderer, kBoardX, kBoardY + y * kCell,
			kBoardX + kBoardW - 1, kBoardY + y * kCell);
	}

	const Board& board = sim.board();
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			const int form = board.at(x, y);
			if (form >= 0) {
				draw_cell(renderer, kBoardX + x * kCell, kBoardY + y * kCell,
					kFormColors[std::min(form, 7)]);
			}
		}
	}

	if (sim.entry() && sim.piece().form <= 6) {
		const Piece& piece = sim.piece();
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		const Piece ghost = board.dropped(piece);
		if (ghost.y != piece.y) {
			SDL_Color faint = kFormColors[piece.form];
			faint.a = 70;
			for (const Offset cell : cells_of(ghost)) {
				if (cell.y >= 0) {
					draw_cell(renderer, kBoardX + cell.x * kCell,
						kBoardY + cell.y * kCell, faint);
				}
			}
		}
		for (const Offset cell : cells_of(piece)) {
			if (cell.y >= 0) {
				draw_cell(renderer, kBoardX + cell.x * kCell,
					kBoardY + cell.y * kCell, kFormColors[piece.form]);
			}
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	// The hold box and the coming pieces.
	fill(renderer, kBoardX - 122, kBoardY, 104, 86, {20, 26, 34, 255});
	draw_preview(renderer, sim.stored(), kBoardX - 122 + 16, kBoardY + 12, 18);
	const auto& queue = sim.queue();
	for (int slot = 0; slot < 5 && slot < static_cast<int>(queue.size()); ++slot) {
		fill(renderer, kBoardX + kBoardW + 18, kBoardY + slot * 92, 104, 86,
			{20, 26, 34, 255});
		draw_preview(renderer, queue[slot],
			kBoardX + kBoardW + 18 + 16, kBoardY + slot * 92 + 12, 18);
	}

	// The forced drop meter: how much of the piece's stay is spent.
	if (app.config.forced_delay > 0.) {
		fill(renderer, kBoardX, kBoardY + kBoardH + 10, kBoardW, 8, {26, 33, 44, 255});
		const auto elapsed = sim.piece_elapsed();
		if (elapsed.has_value()) {
			const double part =
				std::min(1.0, *elapsed / app.config.forced_delay);
			const Uint8 red = static_cast<Uint8>(90 + 165 * part);
			const Uint8 green = static_cast<Uint8>(200 - 140 * part);
			fill(renderer, kBoardX, kBoardY + kBoardH + 10,
				static_cast<int>(kBoardW * (1.0 - part)), 8, {red, green, 80, 255});
		}
	}
}

// --- The ImGui layers: labels, stat panels, menus. -------------------------

void draw_label (const char* text, float x, float y, ImU32 color = IM_COL32(150, 165, 185, 255)) {
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), color, text);
}

void draw_banner (App& app) {
	const Banner& banner = app.session->banner();
	if (banner.frame < 0) {
		return;
	}
	const long age = app.session->sim().frame() - banner.frame;
	if (age > 70) {
		return;
	}
	const float alpha = age < 50 ? 1.f : 1.f - (age - 50) / 20.f;
	ImFont* font = ImGui::GetFont();
	const float size = ImGui::GetFontSize() * 1.35f;
	const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, banner.text.c_str());
	ImGui::GetForegroundDrawList()->AddText(font, size,
		ImVec2(kBoardX + (kBoardW - extent.x) / 2, kBoardY - 34),
		IM_COL32(255, 210, 74, static_cast<int>(alpha * 255)), banner.text.c_str());
}

void draw_stat_panels (App& app) {
	const ImVec2 origin(kBoardX + kBoardW + 140.f, static_cast<float>(kBoardY));
	for (const StatDef& stat : all_stats()) {
		const auto found = app.config.stats.find(stat.id);
		if (found == app.config.stats.end() || !found->second.shown) {
			continue;
		}
		StatSpot& spot = found->second;
		if (app.place_panels || !app.editing) {
			ImGui::SetNextWindowPos(ImVec2(origin.x + spot.x, origin.y + spot.y));
		}
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
		if (!app.editing) {
			flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg,
			app.editing ? ImVec4(0.16f, 0.22f, 0.32f, 0.9f) : ImVec4(0.07f, 0.09f, 0.12f, 0.65f));
		ImGui::Begin((std::string("stat##") + stat.id).c_str(), nullptr, flags);
		ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", stat.label);
		ImGui::SetWindowFontScale(1.5f);
		ImGui::Text("%s", stat.value(*app.session).c_str());
		ImGui::SetWindowFontScale(1.f);
		if (app.editing && !app.place_panels) {
			// Dragging writes straight back into the layout being saved.
			const ImVec2 where = ImGui::GetWindowPos();
			spot.x = where.x - origin.x;
			spot.y = where.y - origin.y;
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
	app.place_panels = false;
}

void draw_layout_editor (App& app) {
	ImGui::SetNextWindowPos(ImVec2(20, 48), ImGuiCond_Appearing);
	ImGui::Begin("Stat layout", &app.editing,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextWrapped("Drag any panel where you want it. Tick a stat to add it.");
	ImGui::Separator();
	for (const StatDef& stat : all_stats()) {
		StatSpot& spot = app.config.stats[stat.id];
		if (ImGui::Checkbox(stat.label, &spot.shown) && spot.shown) {
			app.place_panels = true;
		}
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Presets");
	for (const std::string& name : preset_names()) {
		if (ImGui::Button(name.c_str())) {
			apply_preset(app.config, name);
			app.place_panels = true;
		}
		ImGui::SameLine();
	}
	ImGui::NewLine();
	if (ImGui::Button("Done")) {
		app.editing = false;
	}
	ImGui::End();
	if (!app.editing) {
		save_config(app.config, app.config_file);
	}
}

void draw_settings (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 80),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.f));
	ImGui::Begin("Settings", &app.show_settings,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextUnformatted("Handling");
	ImGui::SliderInt("DAS (ms)", &app.config.das, 0, 330);
	ImGui::SliderInt("ARR (ms)", &app.config.arr, 0, 83);
	ImGui::SliderInt("DCD (ms)", &app.config.dcd, 0, 330);
	ImGui::SliderInt("SDF (x, 40 = instant)", &app.config.sdf, 5, 40);
	ImGui::SliderInt("ARE (ms)", &app.config.are, 0, 500);
	ImGui::Separator();
	ImGui::TextUnformatted("Training");
	float forced = static_cast<float>(app.config.forced_delay);
	if (ImGui::SliderFloat("Forced drop (s, 0 = off)", &forced, 0.f, 5.f, "%.1f")) {
		app.config.forced_delay = forced;
	}
	const char* spin_rules[] = {"Off", "T-spins", "All spins", "All spins + minis"};
	ImGui::Combo("Spins", &app.config.spin_rule, spin_rules, 4);
	const char* finesse_rules[] = {"Off", "Count faults", "Retry on fault"};
	ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
	ImGui::Checkbox("Wall kicks", &app.config.kicks);
	ImGui::Separator();
	ImGui::TextDisabled("Keys: arrows move, Z/X/A turn, space drops,\n"
		"down soft drops, shift or C holds, escape pauses.");
	ImGui::TextDisabled("Handling applies from the next game.");
	if (ImGui::Button("Save")) {
		save_config(app.config, app.config_file);
		app.show_settings = false;
	}
	ImGui::End();
}

void draw_summary (const Session& session) {
	// The figures a finished game is judged by, whatever panels were up.
	const struct { const char* label; const char* id; } rows[] = {
		{"Time", "time"}, {"Pieces", "pieces"}, {"PPS", "pps"},
		{"Lines", "lines"}, {"Attack", "attack"}, {"APM", "apm"},
		{"VS", "vs"}, {"Finesse", "finesse"}, {"Best B2B / combo", "b2b"},
	};
	if (ImGui::BeginTable("summary", 2)) {
		for (const auto& row : rows) {
			for (const StatDef& stat : all_stats()) {
				if (std::string(stat.id) == row.id) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", row.label);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(stat.value(session).c_str());
				}
			}
		}
		ImGui::EndTable();
	}
}

void draw_menus (App& app) {
	const ImVec2 middle(ImGui::GetIO().DisplaySize.x / 2,
		ImGui::GetIO().DisplaySize.y / 2);
	const ImGuiWindowFlags box = ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings;

	if (app.screen == Screen::Menu) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("main menu", nullptr, box);
		ImGui::SetWindowFontScale(1.8f);
		ImGui::TextUnformatted("FORCETRIS");
		ImGui::SetWindowFontScale(1.f);
		ImGui::TextDisabled("the forced hard drop trainer");
		ImGui::Spacing();
		if (ImGui::Button("Play", ImVec2(220, 0))) {
			start_game(app);
		}
		if (ImGui::Button("Settings", ImVec2(220, 0))) {
			app.show_settings = true;
		}
		if (ImGui::Button("Quit", ImVec2(220, 0))) {
			app.quit = true;
		}
		ImGui::End();
	} else if (app.screen == Screen::Game && app.paused) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("paused", nullptr, box);
		ImGui::TextUnformatted("Paused");
		ImGui::Spacing();
		if (ImGui::Button("Resume", ImVec2(220, 0))) {
			app.paused = false;
		}
		if (ImGui::Button("Restart", ImVec2(220, 0))) {
			start_game(app);
		}
		if (ImGui::Button("Edit stat layout", ImVec2(220, 0))) {
			app.paused = false;
			app.editing = true;
			app.place_panels = true;
		}
		if (ImGui::Button("Settings", ImVec2(220, 0))) {
			app.show_settings = true;
		}
		if (ImGui::Button("Back to menu", ImVec2(220, 0))) {
			app.screen = Screen::Menu;
		}
		ImGui::End();
	} else if (app.screen == Screen::Over) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("game over", nullptr, box);
		ImGui::SetWindowFontScale(1.4f);
		ImGui::TextUnformatted("Game over");
		ImGui::SetWindowFontScale(1.f);
		ImGui::Spacing();
		draw_summary(*app.session);
		ImGui::Spacing();
		if (ImGui::Button("Play again", ImVec2(220, 0))) {
			start_game(app);
		}
		if (ImGui::Button("Back to menu", ImVec2(220, 0))) {
			app.screen = Screen::Menu;
		}
		ImGui::End();
	}

	if (app.show_settings) {
		draw_settings(app);
	}
}

// --- The loop. -------------------------------------------------------------

int run (bool smoke, long smoke_frames) {
	App app;
	app.config_file = config_path();
	app.config = load_config(app.config_file);

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return 1;
	}
	app.window = SDL_CreateWindow("Forcetris",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1180, 700,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (app.window == nullptr) {
		SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
		return 1;
	}
	app.renderer = SDL_CreateRenderer(app.window, -1,
		smoke ? SDL_RENDERER_SOFTWARE
		      : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (app.renderer == nullptr) {
		app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (app.renderer == nullptr) {
		SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
		return 1;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	ImGui::GetStyle().WindowRounding = 6.f;
	ImFontConfig font;
	font.SizePixels = 17.f;
	ImGui::GetIO().Fonts->AddFontDefault(&font);
	ImGui_ImplSDL2_InitForSDLRenderer(app.window, app.renderer);
	ImGui_ImplSDLRenderer2_Init(app.renderer);

	std::mt19937 mash(20260818);
	if (smoke) {
		start_game(app);
	}

	Uint64 previous = SDL_GetPerformanceCounter();
	double behind = 0.;
	long frames = 0;
	while (!app.quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			handle_event(app, event);
		}

		if (smoke) {
			// Button mashing at a fixed seed: the point is that the whole loop
			// runs, draws and shuts down without a display or a player.
			if (frames % 3 == 0) {
				const Key keys[] = {Key::Left, Key::Right, Key::Soft, Key::Hard,
					Key::Hold, Key::Ccw, Key::Cw, Key::Flip};
				app.session->key(keys[mash() % 8], mash() % 4 != 0);
			}
			behind = 0.02;
		} else {
			const Uint64 now = SDL_GetPerformanceCounter();
			behind += static_cast<double>(now - previous)
				/ SDL_GetPerformanceFrequency();
			previous = now;
			behind = std::min(behind, 0.25);
		}

		while (behind >= 0.02) {
			behind -= 0.02;
			if (app.screen == Screen::Game && !app.paused && !app.editing) {
				if (!app.session->step()) {
					app.screen = Screen::Over;
				}
				++frames;
			}
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		SDL_SetRenderDrawColor(app.renderer, 12, 15, 20, 255);
		SDL_RenderClear(app.renderer);
		if (app.session.has_value() && app.screen != Screen::Menu) {
			draw_board(app);
			draw_label("HOLD", kBoardX - 122.f, kBoardY - 22.f);
			draw_label("NEXT", kBoardX + kBoardW + 18.f, kBoardY - 22.f);
			draw_stat_panels(app);
			draw_banner(app);
		}
		if (app.editing) {
			draw_layout_editor(app);
		}
		draw_menus(app);

		ImGui::Render();
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);

		if (smoke && frames >= smoke_frames) {
			// A picture of the last frame, before the present wipes it, so a
			// headless run can be looked at rather than taken on faith.
			if (const char* shot = std::getenv("FORCETRIS_SHOT")) {
				int w = 0;
				int h = 0;
				SDL_GetRendererOutputSize(app.renderer, &w, &h);
				SDL_Surface* grab = SDL_CreateRGBSurfaceWithFormat(
					0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
				if (grab != nullptr && SDL_RenderReadPixels(app.renderer, nullptr,
					grab->format->format, grab->pixels, grab->pitch) == 0) {
					SDL_SaveBMP(grab, shot);
				}
				SDL_FreeSurface(grab);
			}
		}
		SDL_RenderPresent(app.renderer);

		if (smoke) {
			if (app.screen == Screen::Over) {
				start_game(app);
			}
			if (frames >= smoke_frames) {
				app.quit = true;
			}
		}
	}

	save_config(app.config, app.config_file);
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(app.renderer);
	SDL_DestroyWindow(app.window);
	SDL_Quit();
	if (smoke) {
		SDL_Log("smoke: ran %ld frames", frames);
	}
	return 0;
}

} // namespace
} // namespace gui
} // namespace forcetris

int main (int, char**) {
	long smoke_frames = 0;
	if (const char* smoke = std::getenv("FORCETRIS_SMOKE")) {
		smoke_frames = std::strtol(smoke, nullptr, 10);
	}
	return forcetris::gui::run(smoke_frames > 0, smoke_frames);
}
