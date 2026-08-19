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
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <random>
#include <string>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "audio.hpp"
#include "config.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/replay.hpp"
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

enum class Screen { Menu, Game, Over, Replays, Viewer, Scores };

// A replay being watched: which placement, which stop along its journey.
struct Viewing {
	replay::Replay game;
	int index = 0;
	int step = 0;
	bool playing = true;
	int speed = 1;
	bool fixed = false;      // Walk the finesse routes instead of the trails.
	double carry = 0.;
	Screen back = Screen::Menu;
};

struct App {
	SDL_Window* window = nullptr;
	SDL_Renderer* renderer = nullptr;
	Config config;
	std::string config_file;
	std::string root;
	Audio audio;
	std::optional<Session> session;
	std::optional<replay::Replay> last_replay;
	std::vector<replay::Replay> shelf;   // The browser's listing.
	std::optional<Viewing> viewing;
	Screen screen = Screen::Menu;
	bool paused = false;
	bool editing = false;        // The stat layout editor is live.
	bool show_settings = false;
	bool place_panels = false;   // Push saved positions into ImGui this frame.
	std::string rebinding;       // Action waiting for its next key, if any.
	int mode = 0;                // The gametype the current game was started as.
	int score_page = 2;          // The high score table being looked at.
	int hiscore_place = -1;      // Where the finished game would place, if it does.
	char name_entry[9] = "";
	bool score_saved = false;
	std::mt19937 seeds{std::random_device{}()};
	bool quit = false;
};

const char* gametype_name (int mode) {
	return mode == 1 ? "timed" : mode == 2 ? "arcade" : "free";
}

std::string place_string (int at) {
	if (at == 0) return "1st";
	if (at == 1) return "2nd";
	if (at == 2) return "3rd";
	return std::to_string(at + 1) + "th";
}

replay::Meta meta_for (const Config& config, int mode) {
	replay::Meta meta;
	char stamp[32] = "";
	const std::time_t now = std::time(nullptr);
	if (std::tm* local = std::localtime(&now)) {
		std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", local);
	}
	meta.played = stamp;
	meta.gametype = gametype_name(mode);
	meta.forced_delay = config.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.spinrule = config.spin_rule;
	meta.cleartype = config.cleartype;
	meta.das = config.das;
	meta.arr = config.arr;
	meta.dcd = config.dcd;
	meta.sdf = config.sdf;
	meta.are = config.are;
	return meta;
}

void start_game (App& app, int mode) {
	app.mode = mode;
	SimConfig config = app.config.sim();
	config.gametype = mode;
	app.session.emplace(config, app.seeds(), meta_for(app.config, mode));
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.audio.start_music();
}

void end_game (App& app) {
	// Saved rather than offered, the way the Python game does it: the moment
	// a run ends is the worst moment to ask someone whether they will want
	// to look at it.
	app.screen = Screen::Over;
	app.audio.fade_music(2.5);
	app.last_replay = app.session->finish();
	if (app.last_replay.has_value()) {
		replay::save(*app.last_replay, replay::folder(app.root));
	}
	// Would this run make the table? The probe carries the raw clock value,
	// exactly as eval_loss probes it - the conversion to stored centiseconds
	// only happens if a name is entered and the score actually submitted.
	// The loss-time counters: eval_loss probes before a still-resolving
	// clear lands its points, so the snapshot does too.
	const Sim& sim = app.session->sim();
	hiscore::Entry probe;
	probe.score = static_cast<std::uint64_t>(
		std::max<long long>(0, sim.final_score()));
	probe.lines = static_cast<std::uint32_t>(std::max(0, sim.final_lines()));
	probe.timer = static_cast<std::uint32_t>(std::max(0L, sim.timer_ms()));
	const int at = hiscore::place(
		hiscore::load(hiscore::folder(app.root)), gametype_name(app.mode), probe);
	app.hiscore_place = at < hiscore::kPerTable ? at : -1;
	app.name_entry[0] = '\0';
	app.score_saved = false;
}

void watch (App& app, replay::Replay game, Screen back) {
	app.viewing.emplace();
	app.viewing->game = std::move(game);
	app.viewing->back = back;
	app.screen = Screen::Viewer;
}

// --- Input. ----------------------------------------------------------------

std::optional<Key> key_for (const Config& config, SDL_Scancode code) {
	for (const ActionDef& action : all_actions()) {
		const auto found = config.keys.find(action.id);
		if (found == config.keys.end()) {
			continue;
		}
		for (const int bound : found->second) {
			if (bound == code) {
				return action.key;
			}
		}
	}
	return std::nullopt;
}

// The next key pressed becomes the binding being waited for. A key is bound
// to one action at a time, so it is taken away from wherever it was.
void take_binding (App& app, SDL_Scancode code) {
	for (auto& [action, codes] : app.config.keys) {
		codes.erase(std::remove(codes.begin(), codes.end(), static_cast<int>(code)),
			codes.end());
	}
	app.config.keys[app.rebinding].push_back(code);
	app.rebinding.clear();
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
	if (down && !app.rebinding.empty()) {
		// Escape stays the pause key, so it backs out rather than binding.
		if (event.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
			take_binding(app, event.key.keysym.scancode);
		} else {
			app.rebinding.clear();
		}
		return;
	}
	if (down && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
		if (app.screen == Screen::Game) {
			if (app.editing) {
				app.editing = false;
			} else {
				app.paused = !app.paused;
			}
		} else if (app.screen == Screen::Viewer && app.viewing.has_value()) {
			app.screen = app.viewing->back;
			app.viewing.reset();
		} else if (app.screen == Screen::Replays
			|| app.screen == Screen::Scores) {
			app.screen = Screen::Menu;
		}
		return;
	}
	if (app.screen != Screen::Game || app.paused || app.editing
		|| ImGui::GetIO().WantCaptureKeyboard) {
		return;
	}
	if (const auto key = key_for(app.config, event.key.keysym.scancode)) {
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
	const char* clear_styles[] = {"Naive", "Sticky cascade", "Linked cascade"};
	ImGui::Combo("Line clears", &app.config.cleartype, clear_styles, 3);
	const char* finesse_rules[] = {"Off", "Count faults", "Retry on fault"};
	ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
	ImGui::Checkbox("Wall kicks", &app.config.kicks);
	ImGui::Separator();
	ImGui::TextUnformatted("Sound");
	int sfx = static_cast<int>(std::lround(app.config.sfx_volume * 100.f));
	if (ImGui::SliderInt("Effects (%)", &sfx, 0, 100)) {
		app.config.sfx_volume = sfx / 100.f;
		app.audio.set_sfx_volume(app.config.sfx_volume);
	}
	int music = static_cast<int>(std::lround(app.config.music_volume * 100.f));
	if (ImGui::SliderInt("Music (%)", &music, 0, 100)) {
		app.config.music_volume = music / 100.f;
		app.audio.set_music_volume(app.config.music_volume);
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Keys");
	for (const ActionDef& action : all_actions()) {
		ImGui::PushID(action.id);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(action.label);
		ImGui::SameLine(140);
		std::vector<int>& codes = app.config.keys[action.id];
		int remove_at = -1;
		for (size_t i = 0; i < codes.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(codes[i]));
			if (ImGui::SmallButton(name != nullptr && *name != '\0' ? name : "?")) {
				remove_at = static_cast<int>(i);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Click to unbind");
			}
			ImGui::PopID();
			ImGui::SameLine();
		}
		if (remove_at >= 0) {
			codes.erase(codes.begin() + remove_at);
		}
		if (app.rebinding == action.id) {
			ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f), "press a key...");
		} else if (ImGui::SmallButton("+")) {
			// The next key pressed lands here; escape backs out. A key taken
			// from another action leaves it, so nothing fires twice.
			app.rebinding = action.id;
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Reset keys")) {
		app.config.keys = default_keys();
		app.rebinding.clear();
	}
	ImGui::Separator();
	ImGui::TextDisabled("Escape pauses and cannot be bound.");
	ImGui::TextDisabled("Handling applies from the next game.");
	if (ImGui::Button("Save")) {
		save_config(app.config, app.config_file);
		app.show_settings = false;
	}
	ImGui::End();
	if (!app.show_settings) {
		app.rebinding.clear();
	}
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

// --- The replay viewer: a re-enactment, never a re-simulation. ------------

void advance_viewer (App& app) {
	// One 20ms tick of watching: a stop every 0.16 seconds at single speed.
	Viewing& show = *app.viewing;
	if (!show.playing || show.game.placements.empty()) {
		return;
	}
	show.carry += 0.02 * show.speed;
	while (show.carry >= 0.16) {
		show.carry -= 0.16;
		const auto stops = show.game.placements[show.index].steps(show.fixed);
		if (show.step + 1 < static_cast<int>(stops.size())) {
			++show.step;
		} else if (show.index + 1 < static_cast<int>(show.game.placements.size())) {
			++show.index;
			show.step = 0;
		} else {
			show.playing = false;
		}
	}
}

void draw_row_strings (App& app, const std::vector<std::string>& rows) {
	SDL_Renderer* renderer = app.renderer;
	fill(renderer, kBoardX - 3, kBoardY - 3, kBoardW + 6, kBoardH + 6, {32, 40, 53, 255});
	fill(renderer, kBoardX, kBoardY, kBoardW, kBoardH, {14, 18, 24, 255});
	for (size_t y = 0; y < rows.size() && y < kHeight; ++y) {
		for (size_t x = 0; x < rows[y].size() && x < kWidth; ++x) {
			const char cell = rows[y][x];
			if (cell >= '0' && cell <= '7') {
				draw_cell(renderer, kBoardX + static_cast<int>(x) * kCell,
					kBoardY + static_cast<int>(y) * kCell, kFormColors[cell - '0']);
			}
		}
	}
}

void draw_viewer (App& app) {
	Viewing& show = *app.viewing;
	if (show.game.placements.empty()) {
		return;
	}
	show.index = std::clamp(
		show.index, 0, static_cast<int>(show.game.placements.size()) - 1);
	const replay::Placement& place = show.game.placements[show.index];
	const auto stops = place.steps(show.fixed);
	show.step = std::clamp(show.step, 0, static_cast<int>(stops.size()) - 1);

	// The board the placement was made onto, with the piece walking over it.
	draw_row_strings(app, replay::padded(show.game.before(show.index)));
	const auto& stop = stops[show.step];
	const Piece piece{place.form, stop[0], stop[1], stop[2]};
	if (place.form >= 0 && place.form <= 6) {
		for (const Offset cell : cells_of(piece)) {
			if (cell.y >= 0 && cell.y < kHeight) {
				draw_cell(app.renderer, kBoardX + cell.x * kCell,
					kBoardY + cell.y * kCell, kFormColors[place.form]);
			}
		}
	}

	// What the player could see at the time: the hold box and the previews.
	fill(app.renderer, kBoardX - 122, kBoardY, 104, 86, {20, 26, 34, 255});
	draw_preview(app.renderer, place.stored, kBoardX - 122 + 16, kBoardY + 12, 18);
	for (size_t slot = 0; slot < place.queue.size() && slot < 3; ++slot) {
		fill(app.renderer, kBoardX + kBoardW + 18,
			kBoardY + static_cast<int>(slot) * 92, 104, 86, {20, 26, 34, 255});
		draw_preview(app.renderer, place.queue[slot],
			kBoardX + kBoardW + 18 + 16,
			kBoardY + static_cast<int>(slot) * 92 + 12, 18);
	}

	// The placement, in words.
	ImGui::SetNextWindowPos(ImVec2(kBoardX + kBoardW + 140.f, kBoardY));
	ImGui::Begin("viewer info", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("Placement %d of %d", show.index + 1,
		static_cast<int>(show.game.placements.size()));
	std::string presses;
	for (const std::string& press : place.presses_shown(show.fixed)) {
		presses += (presses.empty() ? "" : " ") + press;
	}
	ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "Presses: %s",
		presses.empty() ? "none" : presses.c_str());
	if (place.judged && place.best.has_value()) {
		ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f),
			"Best: %d press%s%s", *place.best, *place.best == 1 ? "" : "es",
			place.wasted() > 0 && !show.fixed ? "  (wasted)" : "");
	}
	if (!place.spin.empty()) {
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f), "%s", place.spin.c_str());
	}
	if (place.lines > 0) {
		ImGui::Text("Cleared %d", place.lines);
	}
	if (place.perfect) {
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f), "PERFECT CLEAR");
	}
	if (place.attack > 0) {
		ImGui::Text("Attack +%d", place.attack);
	}
	ImGui::Text("Score %lld", place.score);
	ImGui::Text("At %.2fs", place.elapsed);
	ImGui::End();

	// The controls, mouse-first.
	ImGui::SetNextWindowPos(ImVec2(kBoardX - 3.f, kBoardY + kBoardH + 26.f));
	ImGui::Begin("viewer controls", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::Button(show.playing ? "Pause" : "Play")) {
		show.playing = !show.playing;
		if (show.playing && show.index + 1 == static_cast<int>(show.game.placements.size())
			&& show.step + 1 == static_cast<int>(stops.size())) {
			show.index = 0;
			show.step = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("|<")) {
		show.index = std::max(0, show.index - 1);
		show.step = 0;
	}
	ImGui::SameLine();
	if (ImGui::Button(">|")) {
		show.index = std::min(
			static_cast<int>(show.game.placements.size()) - 1, show.index + 1);
		show.step = 0;
	}
	ImGui::SameLine();
	const char* speeds[] = {"1x", "2x", "4x"};
	int gear = show.speed == 4 ? 2 : show.speed - 1;
	ImGui::SetNextItemWidth(60);
	if (ImGui::Combo("##speed", &gear, speeds, 3)) {
		show.speed = gear == 2 ? 4 : gear + 1;
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Perfect finesse", &show.fixed)) {
		// The stops change under the piece, so start this placement over.
		show.step = 0;
	}
	ImGui::SameLine();
	if (ImGui::Button("Back")) {
		app.screen = show.back;
		app.viewing.reset();
	}
	ImGui::End();
}

void draw_replays (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 60),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(560, 0));
	ImGui::Begin("Replays", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if (app.shelf.empty()) {
		ImGui::TextDisabled("No replays yet. Finish a game first.");
	}
	for (size_t i = 0; i < app.shelf.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(app.shelf[i].title().c_str());
		ImGui::SameLine(430);
		if (ImGui::SmallButton("Watch")) {
			watch(app, app.shelf[i], Screen::Replays);
		}
		ImGui::PopID();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(120, 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

void draw_scores (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, 50),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(640, 0));
	ImGui::Begin("High scores", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	static const char* kPages[] = {"Arcade", "Timed", "Free"};
	for (int page = 0; page < 3; ++page) {
		if (page > 0) {
			ImGui::SameLine();
		}
		if (ImGui::RadioButton(kPages[page], app.score_page == page)) {
			app.score_page = page;
		}
	}
	ImGui::Separator();
	const hiscore::Tables tables = hiscore::load(hiscore::folder(app.root));
	if (ImGui::BeginTable("scores", 4)) {
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Score");
		ImGui::TableSetupColumn("Lines");
		ImGui::TableSetupColumn("Time taken");
		ImGui::TableHeadersRow();
		for (const hiscore::Entry& entry : tables[app.score_page]) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(hiscore::shown_name(entry).c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%llu", static_cast<unsigned long long>(entry.score));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u", entry.lines);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(hiscore::shown_timer(entry.timer).c_str());
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(120, 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
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
			start_game(app, 0);
		}
		if (ImGui::Button("Play timed", ImVec2(220, 0))) {
			start_game(app, 1);
		}
		if (ImGui::Button("Play arcade", ImVec2(220, 0))) {
			start_game(app, 2);
		}
		if (ImGui::Button("High scores", ImVec2(220, 0))) {
			app.score_page = 2;
			app.screen = Screen::Scores;
		}
		if (ImGui::Button("Replays", ImVec2(220, 0))) {
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
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
			start_game(app, app.mode);
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
		if (app.hiscore_place >= 0 && !app.score_saved) {
			ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f),
				"You got the %s place high score!",
				place_string(app.hiscore_place).c_str());
			ImGui::SetNextItemWidth(140);
			ImGui::InputText("##name", app.name_entry, sizeof app.name_entry,
				ImGuiInputTextFlags_CallbackCharFilter,
				[] (ImGuiInputTextCallbackData* data) {
					const ImWchar c = data->EventChar;
					const bool fine = (c >= 'a' && c <= 'z')
						|| (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
						|| c == ' ';
					return fine ? 0 : 1;
				});
			ImGui::SameLine();
			if (ImGui::Button("Save score")) {
				// As the save menu stores it: the name padded to eight, the
				// clock converted to elapsed centiseconds.
				const Sim& sim = app.session->sim();
				hiscore::Entry entry;
				std::string name = app.name_entry;
				name.resize(8, ' ');
				std::copy(name.begin(), name.end(), entry.name.begin());
				entry.score = static_cast<std::uint64_t>(
					std::max<long long>(0, sim.final_score()));
				entry.lines = static_cast<std::uint32_t>(
					std::max(0, sim.final_lines()));
				entry.timer = static_cast<std::uint32_t>(app.mode == 1
					? std::max(0L, (300000L - sim.timer_ms()) / 10)
					: std::max(0L, sim.timer_ms() / 10));
				hiscore::submit(hiscore::folder(app.root),
					gametype_name(app.mode), entry);
				app.score_saved = true;
			}
		} else if (app.score_saved) {
			ImGui::TextDisabled("Score saved.");
		}
		if (app.last_replay.has_value()) {
			if (ImGui::Button("Watch replay", ImVec2(220, 0))) {
				watch(app, *app.last_replay, Screen::Over);
			}
		} else {
			ImGui::TextDisabled("Too short to record.");
		}
		if (ImGui::Button("Play again", ImVec2(220, 0))) {
			start_game(app, app.mode);
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
	// No audio device is not a reason not to play; the mixer just stays shut.
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
		app.root = game_root();
		app.audio.open(app.root);
		app.audio.set_sfx_volume(app.config.sfx_volume);
		app.audio.set_music_volume(app.config.music_volume);
	} else {
		app.root = game_root();
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
		start_game(app, 0);
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
			// Button mashing at a fixed seed, pushed as real SDL key events so
			// the run goes through the binding lookup and not around it. The
			// point is that the whole loop runs, draws and shuts down without
			// a display or a player.
			if (frames % 3 == 0) {
				const auto& actions = all_actions();
				const auto& codes = app.config.keys[actions[mash() % actions.size()].id];
				if (!codes.empty()) {
					SDL_Event pressed{};
					const bool down = mash() % 4 != 0;
					pressed.type = down ? SDL_KEYDOWN : SDL_KEYUP;
					pressed.key.state = down ? SDL_PRESSED : SDL_RELEASED;
					pressed.key.keysym.scancode =
						static_cast<SDL_Scancode>(codes[mash() % codes.size()]);
					pressed.key.keysym.sym =
						SDL_GetKeyFromScancode(pressed.key.keysym.scancode);
					SDL_PushEvent(&pressed);
				}
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
					end_game(app);
				}
				++frames;
			} else if (app.screen == Screen::Viewer && app.viewing.has_value()) {
				advance_viewer(app);
				if (smoke) {
					++frames;
				}
			}
		}
		if (app.session.has_value()) {
			for (const std::string& cue : app.session->take_cues()) {
				app.audio.play(cue);
			}
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		SDL_SetRenderDrawColor(app.renderer, 12, 15, 20, 255);
		SDL_RenderClear(app.renderer);
		if (app.session.has_value()
			&& (app.screen == Screen::Game || app.screen == Screen::Over)) {
			draw_board(app);
			draw_label("HOLD", kBoardX - 122.f, kBoardY - 22.f);
			draw_label("NEXT", kBoardX + kBoardW + 18.f, kBoardY - 22.f);
			draw_stat_panels(app);
			draw_banner(app);
		}
		if (app.screen == Screen::Viewer && app.viewing.has_value()) {
			draw_label("HOLD", kBoardX - 122.f, kBoardY - 22.f);
			draw_label("NEXT", kBoardX + kBoardW + 18.f, kBoardY - 22.f);
			draw_viewer(app);
		}
		if (app.screen == Screen::Replays) {
			draw_replays(app);
		}
		if (app.screen == Screen::Scores) {
			draw_scores(app);
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
				// With FORCETRIS_SMOKE_VIEW set the run ends in the replay
				// viewer instead of another game, so the screenshot shows a
				// recording being re-enacted.
				if (std::getenv("FORCETRIS_SMOKE_VIEW") != nullptr
					&& app.last_replay.has_value()) {
					watch(app, *app.last_replay, Screen::Menu);
				} else {
					start_game(app, app.mode);
				}
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
