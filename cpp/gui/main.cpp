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
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "audio.hpp"
#include "config.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/rating.hpp"
#include "forcetris/replay.hpp"
#include "session.hpp"
#include "stats.hpp"
#include "versus.hpp"

namespace forcetris {
namespace gui {
namespace {

// The board's place on screen. The stat panels anchor to its right edge, so
// their saved positions survive a window resize. Everything is laid out in
// the same 96-dpi units it always was and multiplied once by the display's
// scale, so a laptop at 150% gets the same picture with more pixels in it
// instead of a stretched copy of the 100% one.
float kScale = 1.f;
int kCell = 26;
int kBoardX = 300;
int kBoardY = 48;
int kBoardW = kWidth * kCell;
int kBoardH = kHeight * kCell;

// A design-unit measure scaled to the display, for the layout literals.
float ui (float units) {
	return units * kScale;
}

int px (float units) {
	return static_cast<int>(std::lround(units * kScale));
}

void apply_ui_scale (float scale) {
	kScale = scale;
	kCell = px(26);
	kBoardX = px(300);
	kBoardY = px(48);
	kBoardW = kWidth * kCell;
	kBoardH = kHeight * kCell;
}

// The typefaces, baked from a real vector font when one can be found: the
// stock ImGui font is a 13px bitmap, and every scaled-up use of it is what
// pixelated the old screens.
struct Fonts {
	ImFont* body = nullptr;
	ImFont* head = nullptr;    // Section headers, stat values, screen names.
	ImFont* title = nullptr;   // The FORCETRIS wordmark.
};

// The first face the machine actually has, tried in the order a player is
// likely to own them. FORCETRIS_FONT overrides the lot (a bold companion is
// guessed from the regular's name); missing everything falls back to the
// bitmap font, small enough to stay sharp.
std::string first_file (const std::vector<std::string>& candidates) {
	std::error_code ignored;
	for (const std::string& path : candidates) {
		if (!path.empty() && std::filesystem::is_regular_file(path, ignored)) {
			return path;
		}
	}
	return "";
}

std::pair<std::string, std::string> find_font_files () {
	if (const char* forced = std::getenv("FORCETRIS_FONT")) {
		// Guess the bold companion from the usual naming: X-Regular.ttf has
		// X-Bold.ttf beside it, plain X.ttf might have X-Bold.ttf.
		std::string bold = forced;
		const size_t tagged = bold.rfind("-Regular");
		const size_t dot = bold.rfind('.');
		if (tagged != std::string::npos) {
			bold.replace(tagged, 8, "-Bold");
		} else if (dot != std::string::npos) {
			bold.insert(dot, "-Bold");
		}
		return {forced, first_file({bold})};
	}
	const std::string regular = first_file({
		"C:\\Windows\\Fonts\\segoeui.ttf",
		"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
		"/System/Library/Fonts/Supplemental/Arial.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
	});
	const std::string bold = first_file({
		"C:\\Windows\\Fonts\\segoeuib.ttf",
		"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
		"/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
		"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
	});
	return {regular, bold};
}

Fonts load_fonts () {
	Fonts fonts;
	ImGuiIO& io = ImGui::GetIO();
	const auto [regular, bold] = find_font_files();
	if (!regular.empty()) {
		fonts.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), ui(19.f));
		const std::string& heavy = bold.empty() ? regular : bold;
		fonts.head = io.Fonts->AddFontFromFileTTF(heavy.c_str(), ui(26.f));
		fonts.title = io.Fonts->AddFontFromFileTTF(heavy.c_str(), ui(44.f));
	}
	if (fonts.body == nullptr) {
		SDL_Log("no scalable font found - falling back to the bitmap font");
		fonts.body = io.Fonts->AddFontDefault();
	}
	if (fonts.head == nullptr) {
		fonts.head = fonts.body;
	}
	if (fonts.title == nullptr) {
		fonts.title = fonts.head;
	}
	return fonts;
}

// The theme: the board's own palette carried into the chrome. One accent -
// the I piece's cyan - against slate, rounded corners, and room to breathe;
// the stock StyleColorsDark is a debugger's colour scheme, not a game's.
void apply_theme () {
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 10.f;
	style.ChildRounding = 8.f;
	style.FrameRounding = 6.f;
	style.PopupRounding = 8.f;
	style.GrabRounding = 6.f;
	style.TabRounding = 6.f;
	style.ScrollbarRounding = 8.f;
	style.WindowPadding = ImVec2(20.f, 16.f);
	style.FramePadding = ImVec2(11.f, 6.f);
	style.ItemSpacing = ImVec2(10.f, 9.f);
	style.ItemInnerSpacing = ImVec2(8.f, 6.f);
	style.CellPadding = ImVec2(8.f, 5.f);
	style.ScrollbarSize = 12.f;
	style.GrabMinSize = 12.f;
	style.WindowBorderSize = 1.f;
	style.FrameBorderSize = 0.f;
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.SeparatorTextBorderSize = 2.f;

	const ImVec4 canvas(0.070f, 0.086f, 0.118f, 0.98f);   // #12161E
	const ImVec4 well(0.118f, 0.145f, 0.196f, 1.f);       // #1E2532
	const ImVec4 wellHover(0.153f, 0.188f, 0.251f, 1.f);
	const ImVec4 wellActive(0.180f, 0.227f, 0.306f, 1.f);
	const ImVec4 accent(0.255f, 0.776f, 0.878f, 1.f);     // The I piece.
	const ImVec4 accentDim(0.255f, 0.776f, 0.878f, 0.28f);
	const ImVec4 edge(0.165f, 0.196f, 0.259f, 0.65f);
	const ImVec4 text(0.910f, 0.929f, 0.957f, 1.f);
	const ImVec4 faded(0.486f, 0.529f, 0.596f, 1.f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = text;
	colors[ImGuiCol_TextDisabled] = faded;
	colors[ImGuiCol_WindowBg] = canvas;
	colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.106f, 0.145f, 0.98f);
	colors[ImGuiCol_Border] = edge;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_FrameBg] = well;
	colors[ImGuiCol_FrameBgHovered] = wellHover;
	colors[ImGuiCol_FrameBgActive] = wellActive;
	colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.067f, 0.094f, 1.f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.078f, 0.098f, 0.137f, 1.f);
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_ScrollbarGrab] = well;
	colors[ImGuiCol_ScrollbarGrabHovered] = wellHover;
	colors[ImGuiCol_ScrollbarGrabActive] = wellActive;
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.86f, 0.94f, 1.f);
	colors[ImGuiCol_Button] = well;
	colors[ImGuiCol_ButtonHovered] = wellHover;
	colors[ImGuiCol_ButtonActive] = wellActive;
	colors[ImGuiCol_Header] = well;
	colors[ImGuiCol_HeaderHovered] = wellHover;
	colors[ImGuiCol_HeaderActive] = wellActive;
	colors[ImGuiCol_Separator] = edge;
	colors[ImGuiCol_SeparatorHovered] = accentDim;
	colors[ImGuiCol_SeparatorActive] = accent;
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_ResizeGripHovered] = accentDim;
	colors[ImGuiCol_ResizeGripActive] = accent;
	colors[ImGuiCol_Tab] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TabHovered] = wellHover;
	colors[ImGuiCol_TabSelected] = well;
	colors[ImGuiCol_TabSelectedOverline] = accent;
	colors[ImGuiCol_TabDimmed] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TabDimmedSelected] = well;
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.086f, 0.106f, 0.145f, 1.f);
	colors[ImGuiCol_TableBorderStrong] = edge;
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.165f, 0.196f, 0.259f, 0.35f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.02f);
	colors[ImGuiCol_TextSelectedBg] = accentDim;
	colors[ImGuiCol_NavHighlight] = accent;
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.6f);

	style.ScaleAllSizes(kScale);
}

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

enum class Screen {
	Menu, Modes, Game, Over, Replays, Viewer, Scores, Help, Analysis };

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
	// The replay the analysis screen is showing, and where it came from.
	std::optional<replay::Replay> studying;
	Screen study_back = Screen::Menu;
	Screen screen = Screen::Menu;
	Screen help_back = Screen::Menu;      // How to play returns where it came from.
	Fonts fonts;
	bool paused = false;
	bool editing = false;        // The stat layout editor is live.
	// The editor opened from the menu, over a throwaway board: closing it
	// drops the preview game and lands back in the settings screen.
	bool layout_preview = false;
	bool show_settings = false;
	bool place_panels = false;   // Push saved positions into ImGui this frame.
	std::string rebinding;       // Action waiting for its next key, if any.
	int mode = 0;                // The gametype the current game was started as.
	int cheese_total = 18;       // The race's quota, set by the mode picker.
	int cheese_period = 250;     // Survival's frames per rising row.
	int cheese_holes = 1;        // Holes per cheese row.
	int cheese_messiness = 100;  // Percent chance a row re-rolls its holes.
	// The match against the bot, when one is on; who it is and how long.
	std::optional<VersusMatch> versus;
	int bot_rank = 4;            // Index into bot::ranks(); 4 is S.
	int first_to = 1;
	int score_page = 2;          // The high score table being looked at.
	int hiscore_place = -1;      // Where the finished game would place, if it does.
	char name_entry[9] = "";
	bool score_saved = false;
	std::mt19937 seeds{std::random_device{}()};
	bool quit = false;
};

const char* gametype_name (int mode) {
	switch (mode) {
		case 1: return "timed";
		case 2: return "arcade";
		case 3: return "cheese_race";
		case 4: return "cheese_survival";
		case 5: return "versus";
		default: return "free";
	}
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
	app.versus.reset();
	app.mode = mode;
	SimConfig config = app.config.sim();
	config.gametype = mode;
	config.cheese_total = app.cheese_total;
	config.cheese_period = app.cheese_period;
	config.cheese_holes = app.cheese_holes;
	config.cheese_messiness = app.cheese_messiness;
	app.session.emplace(config, app.seeds(), meta_for(app.config, mode));
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.audio.start_music();
}

// A match against the bot: the player's session as ever, the opponent and
// the scoreboard beside it.
void start_versus (App& app) {
	app.mode = 5;
	SimConfig config = app.config.sim();
	config.gametype = 5;
	config.cheese_holes = 1;
	config.cheese_messiness = 30;
	const replay::Meta meta = meta_for(app.config, 5);
	app.session.emplace(config, app.seeds(), meta);
	app.versus.emplace(app.bot_rank, app.first_to);
	app.versus->begin_round(config, app.seeds(), meta);
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.audio.start_music();
}

// The finished round, both boards: the player's replay with the bot's side
// embedded under it. Nothing when the player's half was too short to keep.
std::optional<replay::Replay> finish_round (App& app) {
	std::optional<replay::Replay> done = app.session->finish();
	if (done.has_value() && app.versus.has_value()
		&& app.versus->bot.has_value()) {
		if (auto other = app.versus->bot->finish(true)) {
			done->opponent.emplace(replay::Opponent{
				std::move(other->meta), std::move(other->placements)});
		}
	}
	return done;
}

void next_versus_round (App& app) {
	// The round just decided is a game in its own right: save it - both
	// boards - before the fresh sessions sweep it away.
	if (auto done = finish_round(app)) {
		replay::save(*done, replay::folder(app.root));
	}
	SimConfig config = app.config.sim();
	config.gametype = 5;
	config.cheese_holes = 1;
	config.cheese_messiness = 30;
	const replay::Meta meta = meta_for(app.config, 5);
	app.session.emplace(config, app.seeds(), meta);
	app.versus->round += 1;
	app.versus->begin_round(config, app.seeds(), meta);
}

void end_game (App& app) {
	// Saved rather than offered, the way the Python game does it: the moment
	// a run ends is the worst moment to ask someone whether they will want
	// to look at it.
	app.screen = Screen::Over;
	app.audio.fade_music(2.5);
	app.last_replay = finish_round(app);
	if (app.last_replay.has_value()) {
		replay::save(*app.last_replay, replay::folder(app.root));
	}
	// Would this run make the table? The probe carries the raw clock value,
	// exactly as eval_loss probes it - the conversion to stored centiseconds
	// only happens if a name is entered and the score actually submitted.
	// The loss-time counters: eval_loss probes before a still-resolving
	// clear lands its points, so the snapshot does too.
	if (app.mode >= 3) {
		// The cheese modes are this side's own; the score file is the Python
		// game's, three tables and no more, and stays byte-compatible.
		app.hiscore_place = -1;
	} else {
		const Sim& sim = app.session->sim();
		hiscore::Entry probe;
		probe.score = static_cast<std::uint64_t>(
			std::max<long long>(0, sim.final_score()));
		probe.lines = static_cast<std::uint32_t>(std::max(0, sim.final_lines()));
		probe.timer = static_cast<std::uint32_t>(std::max(0L, sim.timer_ms()));
		const int at = hiscore::place(
			hiscore::load(hiscore::folder(app.root)), gametype_name(app.mode), probe);
		app.hiscore_place = at < hiscore::kPerTable ? at : -1;
	}
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
		} else if (app.screen == Screen::Analysis) {
			app.screen = app.study_back;
			app.studying.reset();
		} else if (app.screen == Screen::Modes) {
			app.screen = Screen::Menu;
		} else if (app.screen == Screen::Help) {
			app.screen = app.help_back;
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

	fill(renderer, kBoardX - px(3), kBoardY - px(3), kBoardW + px(6), kBoardH + px(6), {32, 40, 53, 255});
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
	fill(renderer, kBoardX - px(122), kBoardY, px(104), px(86), {20, 26, 34, 255});
	draw_preview(renderer, sim.stored(), kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	const auto& queue = sim.queue();
	for (int slot = 0; slot < 5 && slot < static_cast<int>(queue.size()); ++slot) {
		fill(renderer, kBoardX + kBoardW + px(18), kBoardY + slot * px(92), px(104), px(86),
			{20, 26, 34, 255});
		draw_preview(renderer, queue[slot],
			kBoardX + kBoardW + px(18) + px(16), kBoardY + slot * px(92) + px(12), px(18));
	}

	// The forced drop meter: how much of the piece's stay is spent.
	if (app.config.forced_delay > 0.) {
		fill(renderer, kBoardX, kBoardY + kBoardH + px(10), kBoardW, px(8), {26, 33, 44, 255});
		const auto elapsed = sim.piece_elapsed();
		if (elapsed.has_value()) {
			const double part =
				std::min(1.0, *elapsed / app.config.forced_delay);
			const Uint8 red = static_cast<Uint8>(90 + 165 * part);
			const Uint8 green = static_cast<Uint8>(200 - 140 * part);
			fill(renderer, kBoardX, kBoardY + kBoardH + px(10),
				static_cast<int>(kBoardW * (1.0 - part)), px(8), {red, green, 80, 255});
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
	ImFont* font = app.fonts.head;
	const float size = font->FontSize;
	const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, banner.text.c_str());
	ImGui::GetForegroundDrawList()->AddText(font, size,
		ImVec2(kBoardX + (kBoardW - extent.x) / 2, kBoardY - ui(38)),
		IM_COL32(255, 210, 74, static_cast<int>(alpha * 255)), banner.text.c_str());
}

// The other board, small, and the wire's state around both: the bot's
// stack with its live piece, the scoreboard, and each side's incoming
// garbage as a red column beside its board.
void draw_versus_panel (App& app) {
	if (!app.versus.has_value() || !app.versus->bot.has_value()) {
		return;
	}
	VersusMatch& match = *app.versus;
	const Sim& theirs = match.bot->sim();
	SDL_Renderer* renderer = app.renderer;
	const int cell = px(13);
	const int left = px(940);
	const int top = kBoardY + px(40);
	fill(renderer, left - px(2), top - px(2),
		kWidth * cell + px(4), kHeight * cell + px(4), {32, 40, 53, 255});
	fill(renderer, left, top, kWidth * cell, kHeight * cell, {14, 18, 24, 255});
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			const int form = theirs.board().at(x, y);
			if (form >= 0) {
				fill(renderer, left + x * cell + 1, top + y * cell + 1,
					cell - 2, cell - 2, kFormColors[std::min(form, 7)]);
			}
		}
	}
	if (theirs.entry() && theirs.piece().form <= 6) {
		for (const Offset at : cells_of(theirs.piece())) {
			if (at.y >= 0) {
				fill(renderer, left + at.x * cell + 1, top + at.y * cell + 1,
					cell - 2, cell - 2, kFormColors[theirs.piece().form]);
			}
		}
	}
	draw_label("BOT", static_cast<float>(left), top - ui(22));
	// Incoming garbage, as red columns: theirs beside their board, the
	// player's beside the player's.
	const auto meter = [renderer] (int x, int bottom, int rows, int step) {
		const int shown = std::min(rows, kHeight);
		if (shown > 0) {
			fill(renderer, x, bottom - shown * step, px(6), shown * step,
				{224, 82, 82, 255});
		}
	};
	meter(kBoardX - px(10), kBoardY + kBoardH,
		app.session->sim().pending_garbage(), kCell);
	meter(left - px(10), top + kHeight * cell,
		theirs.pending_garbage(), cell);
	// The scoreboard, under the bot's board.
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(left) - ui(4),
		static_cast<float>(top + kHeight * cell) + ui(10)));
	ImGui::Begin("versus score", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("You %d - %d Bot (%s)", match.player_wins, match.bot_wins,
		bot::ranks()[match.rank_index].name);
	ImGui::TextDisabled("first to %d  round %d", match.first_to, match.round);
	const int surge = app.session->sim().surge_charge();
	if (surge > 0) {
		ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
			"SURGE +%d", surge);
	}
	ImGui::End();
	// The round's verdict, front and centre while it lingers.
	if (match.phase != VersusMatch::Phase::Playing) {
		const char* verdict = match.round_draw ? "DRAW - AGAIN"
			: match.round_player_won ? "ROUND WON" : "ROUND LOST";
		ImFont* font = app.fonts.title;
		const ImVec2 extent = font->CalcTextSizeA(
			font->FontSize, FLT_MAX, 0.f, verdict);
		ImGui::GetForegroundDrawList()->AddText(font, font->FontSize,
			ImVec2(kBoardX + (kBoardW - extent.x) / 2,
				kBoardY + kBoardH / 2.f - font->FontSize),
			IM_COL32(255, 210, 74, 255), verdict);
	}
}

void draw_stat_panels (App& app) {
	const ImVec2 origin(kBoardX + kBoardW + ui(140), static_cast<float>(kBoardY));
	for (const StatDef& stat : all_stats()) {
		const auto found = app.config.stats.find(stat.id);
		if (found == app.config.stats.end() || !found->second.shown) {
			continue;
		}
		StatSpot& spot = found->second;
		if (app.place_panels || !app.editing) {
			// The saved layout is in design units, the window in pixels.
			ImGui::SetNextWindowPos(
				ImVec2(origin.x + ui(spot.x), origin.y + ui(spot.y)));
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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui(14), ui(8)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(2)));
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(ui(96), 0.f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin((std::string("stat##") + stat.id).c_str(), nullptr, flags);
		ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", stat.label);
		ImGui::PushFont(app.fonts.head);
		ImGui::Text("%s", stat.value(*app.session).c_str());
		ImGui::PopFont();
		if (app.editing && !app.place_panels) {
			// Dragging writes straight back into the layout being saved,
			// in design units so the file means the same at every scale.
			const ImVec2 where = ImGui::GetWindowPos();
			spot.x = (where.x - origin.x) / kScale;
			spot.y = (where.y - origin.y) / kScale;
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}
	app.place_panels = false;
}

void draw_layout_editor (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ui(20), ui(48)), ImGuiCond_Appearing);
	ImGui::Begin("Stat layout", &app.editing,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextWrapped("Drag any panel where you want it. Tick a stat to add it.");
	ImGui::Separator();
	if (ImGui::BeginTable("editorstats", 2)) {
		for (const StatDef& stat : all_stats()) {
			StatSpot& spot = app.config.stats[stat.id];
			ImGui::TableNextColumn();
			if (ImGui::Checkbox(stat.label, &spot.shown) && spot.shown) {
				app.place_panels = true;
			}
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Presets");
	for (const std::string& name : preset_names()) {
		ImGui::SameLine();
		if (ImGui::Button(name.c_str())) {
			apply_preset(app.config, name);
			app.place_panels = true;
		}
	}
	ImGui::Spacing();
	if (ImGui::Button(app.layout_preview ? "Back to settings" : "Done",
		ImVec2(ui(180), 0))) {
		app.editing = false;
	}
	ImGui::End();
	if (!app.editing) {
		save_config(app.config, app.config_file);
	}
}

// The stat layout editor, reachable from the pause menu and - through a
// throwaway preview board - from the settings screen and the main menu.
void open_layout_editor (App& app) {
	if (!app.session.has_value() || app.screen != Screen::Game) {
		app.session.emplace(app.config.sim(), app.seeds(),
			meta_for(app.config, 0));
		app.layout_preview = true;
		app.screen = Screen::Game;
	}
	app.paused = false;
	app.editing = true;
	app.place_panels = true;
	app.show_settings = false;
}

void draw_settings (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(70)),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(560), 0));
	ImGui::Begin("Settings", &app.show_settings,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::BeginTabBar("settings", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Handling")) {
			ImGui::Spacing();
			ImGui::SliderInt("DAS (ms)", &app.config.das, 0, 330);
			ImGui::SliderInt("ARR (ms)", &app.config.arr, 0, 83);
			ImGui::SliderInt("DCD (ms)", &app.config.dcd, 0, 330);
			ImGui::SliderInt("SDF (x, 40 = instant)", &app.config.sdf, 5, 40);
			ImGui::SliderInt("ARE (ms)", &app.config.are, 0, 500);
			ImGui::Spacing();
			float forced = static_cast<float>(app.config.forced_delay);
			if (ImGui::SliderFloat("Forced drop (s, 0 = off)", &forced,
				0.f, 5.f, "%.1f")) {
				app.config.forced_delay = forced;
			}
			ImGui::Spacing();
			ImGui::TextDisabled("Handling applies from the next game.");
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Rules")) {
			ImGui::Spacing();
			const char* spin_rules[] = {
				"Off", "T-spins", "All spins", "All spins + minis"};
			ImGui::Combo("Spins", &app.config.spin_rule, spin_rules, 4);
			const char* clear_styles[] = {
				"Naive", "Sticky cascade", "Linked cascade"};
			ImGui::Combo("Line clears", &app.config.cleartype, clear_styles, 3);
			const char* finesse_rules[] = {
				"Off", "Count faults", "Retry on fault"};
			ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
			ImGui::Checkbox("Wall kicks", &app.config.kicks);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Sound")) {
			ImGui::Spacing();
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
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Keys")) {
			ImGui::Spacing();
			for (const ActionDef& action : all_actions()) {
				ImGui::PushID(action.id);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(action.label);
				ImGui::SameLine(ui(150));
				std::vector<int>& codes = app.config.keys[action.id];
				int remove_at = -1;
				for (size_t i = 0; i < codes.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					const char* name = SDL_GetScancodeName(
						static_cast<SDL_Scancode>(codes[i]));
					if (ImGui::SmallButton(
						name != nullptr && *name != '\0' ? name : "?")) {
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
					ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f),
						"press a key...");
				} else if (ImGui::SmallButton("+")) {
					// The next key pressed lands here; escape backs out. A key
					// taken from another action leaves it, so nothing fires
					// twice.
					app.rebinding = action.id;
				}
				ImGui::PopID();
			}
			ImGui::Spacing();
			if (ImGui::Button("Reset keys")) {
				app.config.keys = default_keys();
				app.rebinding.clear();
			}
			ImGui::TextDisabled("Escape pauses and cannot be bound.");
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Layout")) {
			ImGui::Spacing();
			ImGui::TextDisabled("The panels beside the board while you play.");
			ImGui::Spacing();
			if (ImGui::BeginTable("statgrid", 2)) {
				for (const StatDef& stat : all_stats()) {
					StatSpot& spot = app.config.stats[stat.id];
					ImGui::TableNextColumn();
					if (ImGui::Checkbox(stat.label, &spot.shown) && spot.shown) {
						app.place_panels = true;
					}
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Presets");
			for (const std::string& name : preset_names()) {
				ImGui::SameLine();
				if (ImGui::Button(name.c_str())) {
					apply_preset(app.config, name);
					app.place_panels = true;
				}
			}
			ImGui::Spacing();
			if (ImGui::Button("Edit stat layout", ImVec2(ui(240), 0))) {
				// Over the running game if one is on, over a preview board
				// if not - either way the panels become draggable.
				open_layout_editor(app);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("drag the panels where you want them");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(ui(140), 0))) {
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

// What the run was made of, as the Python analysis screen lists it. The rows
// come out of the core, where the cross test grades them against that
// screen's own text, so the two games cannot drift apart.
void draw_analysis_rows (const replay::Replay& game) {
	if (ImGui::BeginTable("analysis", 2)) {
		for (const auto& [label, value] : replay::analysis_rows(game)) {
			ImGui::TableNextRow();
			if (label.empty()) {
				// The spacers the Python screen leaves between the groups.
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(" ");
				continue;
			}
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", label.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		}
		ImGui::EndTable();
	}
}

// The analysis of a saved replay, opened from the browser. The loss screen
// shows the same rows inline, since a finished game is already looking at
// them.
// One label-value line in an analysis table.
void detail_row (const char* label, const std::string& value) {
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", label);
	ImGui::TableSetColumnIndex(1);
	ImGui::TextUnformatted(value.c_str());
}

std::string detail_text (const char* spec, ...) {
	char buffer[96];
	va_list args;
	va_start(args, spec);
	std::vsnprintf(buffer, sizeof buffer, spec, args);
	va_end(args);
	return buffer;
}

// The attack tab: what the run sent and dug, per minute, per piece, per
// line - the figures the TetraStats crowd reads a league game by.
void draw_analysis_attack (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const double pieces = std::max(1, s.placements);
	const double seconds = std::max(1e-9, s.seconds);
	const int ds = game.meta.downstack;
	if (ImGui::BeginTable("attack", 2)) {
		detail_row("Attack", detail_text("%d", s.attack));
		detail_row("APM", detail_text("%.1f", s.apm));
		detail_row("APP (attack/piece)", detail_text("%.2f", s.attack / pieces));
		detail_row("APL (attack/line)", detail_text("%.2f",
			s.lines > 0 ? s.attack / static_cast<double>(s.lines) : 0.));
		detail_row("Downstack", detail_text("%d", ds));
		detail_row("DS/piece", detail_text("%.2f", ds / pieces));
		detail_row("DS/second", detail_text("%.2f", ds / seconds));
		detail_row("VS", detail_text("%.1f", s.vs));
		detail_row("VS/APM", detail_text("%.2f",
			s.apm > 0. ? s.vs / s.apm : 0.));
		ImGui::EndTable();
	}
}

// The speed tab: pace, its peak and its halves, and what the hands did.
void draw_analysis_speed (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const auto& places = game.placements;
	const int count = static_cast<int>(places.size());
	double peak = 0.;
	for (int i = 0; i + 9 < count; ++i) {
		const double start = i > 0 ? places[i - 1].elapsed : 0.;
		const double span = places[i + 9].elapsed - start;
		if (span > 0.) {
			peak = std::max(peak, 10. / span);
		}
	}
	const int half = count / 2;
	double first = 0.;
	double second = 0.;
	if (half > 0 && places[half - 1].elapsed > 0.) {
		first = half / places[half - 1].elapsed;
	}
	if (count > half && places[count - 1].elapsed > places[half - 1].elapsed) {
		second = (count - half)
			/ (places[count - 1].elapsed - places[half - 1].elapsed);
	}
	int holds = 0;
	int forced = 0;
	for (const replay::Placement& place : places) {
		holds += place.held ? 1 : 0;
		forced += place.forced ? 1 : 0;
	}
	if (ImGui::BeginTable("speed", 2)) {
		detail_row("PPS", detail_text("%.2f", s.pps));
		detail_row("Peak PPS (10 pieces)", detail_text("%.2f", peak));
		detail_row("First half", detail_text("%.2f PPS", first));
		detail_row("Second half", detail_text("%.2f PPS", second));
		detail_row("Key presses", detail_text("%d", s.presses));
		detail_row("KPP (keys/piece)", detail_text("%.2f", s.ppp));
		detail_row("KPS (keys/second)", detail_text("%.2f",
			s.seconds > 0. ? s.presses / s.seconds : 0.));
		detail_row("Holds", detail_text("%d", holds));
		detail_row("Forced drops", detail_text("%d", forced));
		ImGui::EndTable();
	}
}

// The pieces tab: what was dealt, what it cleared, and how it spun.
void draw_analysis_pieces (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const auto& places = game.placements;
	const double count = std::max<size_t>(1, places.size());
	int forms[7] = {};
	int spins = 0;
	int minis = 0;
	int max_combo = 0;
	int max_b2b = 0;
	for (const replay::Placement& place : places) {
		if (place.form >= 0 && place.form <= 6) {
			++forms[place.form];
		}
		if (!place.spin.empty()) {
			++spins;
			if (place.spin.rfind("MINI", 0) == 0) {
				++minis;
			}
		}
		max_combo = std::max(max_combo, place.combo);
		max_b2b = std::max(max_b2b, place.b2b);
	}
	if (ImGui::BeginTable("pieces", 2)) {
		static const char* names[] = {"I", "O", "T", "S", "Z", "J", "L"};
		for (int form = 0; form < 7; ++form) {
			detail_row(names[form], detail_text("%d  (%.0f%%)",
				forms[form], forms[form] * 100. / count));
		}
		static const char* sizes[] = {"Singles", "Doubles", "Triples", "Quads"};
		for (int size = 1; size <= 4; ++size) {
			const auto found = s.clears.find(size);
			detail_row(sizes[size - 1], detail_text("%d",
				found == s.clears.end() ? 0 : found->second));
		}
		detail_row("Spins", detail_text("%d  (%d mini)", spins, minis));
		detail_row("Perfect clears", detail_text("%d", s.perfects));
		detail_row("Best combo", detail_text("%d", max_combo));
		detail_row("Best B2B", detail_text("%d", max_b2b));
		ImGui::EndTable();
	}
}

// The rating tab: where the run's figures sit against the public shape of
// Tetra League. An estimate over community averages, and it says so.
void draw_analysis_rating (App& app, const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const rating::Estimate guess = rating::estimate(s.apm, s.pps, s.vs);
	if (std::string(guess.rank).empty()) {
		ImGui::TextDisabled("Not enough of a game to place.");
		return;
	}
	ImGui::Spacing();
	ImGui::PushFont(app.fonts.title);
	ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f), "%s", guess.rank);
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::PushFont(app.fonts.head);
	ImGui::Text("  %.0f TR", guess.tr);
	ImGui::PopFont();
	ImGui::Spacing();
	if (ImGui::BeginTable("rating", 2)) {
		detail_row("Estimated Glicko", detail_text("%.0f", guess.glicko));
		detail_row("Estimated TR", detail_text("%.0f / 25000", guess.tr));
		detail_row("From APM", detail_text("%.1f", s.apm));
		detail_row("From PPS", detail_text("%.2f", s.pps));
		detail_row("From VS", detail_text("%.1f", s.vs));
		ImGui::EndTable();
	}
	ImGui::Spacing();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui(430));
	ImGui::TextDisabled(
		"An estimate, not a rating: your figures placed against public "
		"Tetra League averages per rank. Only real league games rate.");
	ImGui::PopTextWrapPos();
}

// The analysis of a saved replay, opened from the browser or the loss
// screen: the graded overview first, the deeper cuts on their own tabs.
void draw_analysis (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(6)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(490), 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(6)));
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui(8), ui(3)));
	ImGui::Begin("Analysis", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (app.studying.has_value()) {
		ImGui::TextDisabled("%s", app.studying->title().c_str());
		if (ImGui::BeginTabBar("analysis_tabs")) {
			if (ImGui::BeginTabItem("Overview")) {
				draw_analysis_rows(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Attack")) {
				draw_analysis_attack(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Speed")) {
				draw_analysis_speed(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Pieces")) {
				draw_analysis_pieces(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Rating")) {
				draw_analysis_rating(app, *app.studying);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::Separator();
		if (ImGui::Button("Watch replay", ImVec2(ui(240), 0))) {
			watch(app, *app.studying, Screen::Analysis);
		}
	} else {
		ImGui::TextDisabled("That run was too short to record.");
	}
	if (ImGui::Button("Back", ImVec2(ui(240), 0))) {
		app.screen = app.study_back;
		app.studying.reset();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

// How to play: the keys as they are bound right now, and the one rule no
// other Tetris has. The Python help screen's text, read off the live
// bindings the same way.
void draw_help (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(50)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(620), 0));
	ImGui::Begin("How to Play", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::BeginTable("keys", 2)) {
		for (const ActionDef& action : all_actions()) {
			// Every key bound to the action, or the word for none - the same
			// line controls.describe() builds.
			std::string bound;
			const auto found = app.config.keys.find(action.id);
			if (found != app.config.keys.end()) {
				for (const int code : found->second) {
					const char* name =
						SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
					bound += (bound.empty() ? "" : ", ")
						+ std::string(name != nullptr && *name != '\0' ? name : "?");
				}
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "%s",
				bound.empty() ? "Unbound" : bound.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action.label);
		}
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "Escape");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Pause");
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Forced Drop");
	if (app.config.forced_delay > 0.) {
		ImGui::Text("Every piece is hard dropped for you %.2fs after it spawns.",
			app.config.forced_delay);
		ImGui::Text("Holding gives the incoming piece a fresh %.2fs, once per piece.",
			app.config.forced_delay);
		ImGui::TextUnformatted("Soft dropping and wall kicks do not stop the clock.");
		ImGui::TextUnformatted("Change the time under Settings.");
	} else {
		ImGui::TextUnformatted("Currently off, so this is plain Tetris.");
		ImGui::TextUnformatted("Turn it on under Settings to train placement speed.");
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(240), 0))) {
		app.screen = app.help_back;
	}
	ImGui::End();
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

void draw_row_strings (App& app, const std::vector<std::string>& rows,
                       int left = kBoardX, int top = kBoardY, int size = kCell) {
	SDL_Renderer* renderer = app.renderer;
	const int w = kWidth * size;
	const int h = kHeight * size;
	fill(renderer, left - px(3), top - px(3), w + px(6), h + px(6), {32, 40, 53, 255});
	fill(renderer, left, top, w, h, {14, 18, 24, 255});
	for (size_t y = 0; y < rows.size() && y < kHeight; ++y) {
		for (size_t x = 0; x < rows[y].size() && x < kWidth; ++x) {
			const char cell = rows[y][x];
			if (cell >= '0' && cell <= '7') {
				draw_cell(renderer, left + static_cast<int>(x) * size,
					top + static_cast<int>(y) * size, kFormColors[cell - '0'],
					size);
			}
		}
	}
}

// The player's clock at the current frame of the re-enactment: a placement
// spans from the previous lock time to its own, walked linearly across its
// stops. At the last stop the clock is exactly the lock time, so the fixed
// (finesse) view - fewer stops, same endpoints - stays in sync.
double viewer_clock (const Viewing& show, size_t stop_count) {
	const auto& places = show.game.placements;
	const double at = places[show.index].elapsed;
	const double from = show.index > 0
		? std::min(places[show.index - 1].elapsed, at) : 0.;
	const double frac = stop_count <= 1 ? 1.
		: static_cast<double>(show.step)
			/ static_cast<double>(stop_count - 1);
	return from + (at - from) * frac;
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

	// The other board, when the file carries one: the bot's own snapshots,
	// shown as they stood at this moment of the player's walk. Stateless -
	// a scan per frame - so scrubbing backwards is just as correct.
	if (show.game.opponent.has_value()) {
		const double clock = viewer_clock(show, stops.size());
		const replay::Placement* seen = nullptr;
		for (const replay::Placement& theirs : show.game.opponent->placements) {
			if (theirs.elapsed <= clock) {
				seen = &theirs;
			}
		}
		// Below the info window, which owns the top of this margin.
		const int mini_left = px(940);
		const int mini_top = kBoardY + px(340);
		draw_row_strings(app,
			replay::padded(seen != nullptr ? seen->rows
				: std::vector<std::string>{}),
			mini_left, mini_top, px(13));
		draw_label("BOT", static_cast<float>(mini_left),
			static_cast<float>(mini_top - ui(22)));
	}

	// What the player could see at the time: the hold box and the previews.
	fill(app.renderer, kBoardX - px(122), kBoardY, px(104), px(86), {20, 26, 34, 255});
	draw_preview(app.renderer, place.stored,
		kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	for (size_t slot = 0; slot < place.queue.size() && slot < 3; ++slot) {
		fill(app.renderer, kBoardX + kBoardW + px(18),
			kBoardY + static_cast<int>(slot) * px(92), px(104), px(86),
			{20, 26, 34, 255});
		draw_preview(app.renderer, place.queue[slot],
			kBoardX + kBoardW + px(18) + px(16),
			kBoardY + static_cast<int>(slot) * px(92) + px(12), px(18));
	}

	// The placement, in words.
	ImGui::SetNextWindowPos(ImVec2(kBoardX + kBoardW + ui(140), kBoardY));
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
	ImGui::SetNextWindowPos(ImVec2(kBoardX - ui(3), kBoardY + kBoardH + ui(14)));
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
	ImGui::SetNextItemWidth(ui(80));
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
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(60)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(580), 0));
	ImGui::Begin("Replays", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (app.shelf.empty()) {
		ImGui::TextDisabled("No replays yet. Finish a game first.");
	}
	for (size_t i = 0; i < app.shelf.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(app.shelf[i].title().c_str());
		ImGui::SameLine(ui(380));
		if (ImGui::SmallButton("Analysis")) {
			app.studying = app.shelf[i];
			app.study_back = Screen::Replays;
			app.screen = Screen::Analysis;
		}
		ImGui::SameLine(ui(470));
		if (ImGui::SmallButton("Watch")) {
			watch(app, app.shelf[i], Screen::Replays);
		}
		ImGui::PopID();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

void draw_scores (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(50)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(660), 0));
	ImGui::Begin("High scores", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
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
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

// One option in a picker row: drawn selected in the accent, and returns
// true when clicked.
bool option_button (const char* label, bool selected, float width) {
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.49f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.58f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.58f, 0.67f, 1.f));
	}
	const bool clicked = ImGui::Button(label, ImVec2(width, 0));
	if (selected) {
		ImGui::PopStyleColor(3);
	}
	return clicked;
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
		ImGui::PushFont(app.fonts.title);
		ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f), "FORCETRIS");
		ImGui::PopFont();
		ImGui::TextDisabled("the forced hard drop trainer");
		ImGui::Dummy(ImVec2(0.f, ui(10)));
		if (ImGui::Button("Play", ImVec2(ui(260), ui(44)))) {
			app.screen = Screen::Modes;
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("How to play", ImVec2(ui(260), 0))) {
			app.help_back = Screen::Menu;
			app.screen = Screen::Help;
		}
		if (ImGui::Button("High scores", ImVec2(ui(260), 0))) {
			app.score_page = 2;
			app.screen = Screen::Scores;
		}
		if (ImGui::Button("Replays", ImVec2(ui(260), 0))) {
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
		}
		if (ImGui::Button("Settings", ImVec2(ui(260), 0))) {
			app.show_settings = true;
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("Quit", ImVec2(ui(260), 0))) {
			app.quit = true;
		}
		ImGui::End();
	} else if (app.screen == Screen::Modes) {
		ImGui::SetNextWindowPos(ImVec2(middle.x, ui(8)),
			ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(7)));
		ImGui::Begin("mode select", nullptr, box);
		ImGui::PushFont(app.fonts.head);
		ImGui::TextUnformatted("Choose a mode");
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		if (ImGui::Button("Free", ImVec2(ui(280), ui(44)))) {
			start_game(app, 0);
		}
		ImGui::TextDisabled("No clock, no ramp: play until you top out.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		if (ImGui::Button("Timed", ImVec2(ui(280), ui(44)))) {
			start_game(app, 1);
		}
		ImGui::TextDisabled("Five minutes; the multiplier climbs as it drains.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		if (ImGui::Button("Arcade", ImVec2(ui(280), ui(44)))) {
			start_game(app, 2);
		}
		ImGui::TextDisabled("The level ramps and garbage rises from below.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Cheese race");
		ImGui::SameLine();
		if (ImGui::Button("10", ImVec2(ui(52), 0))) {
			app.cheese_total = 10;
			start_game(app, 3);
		}
		ImGui::SameLine();
		if (ImGui::Button("18", ImVec2(ui(52), 0))) {
			app.cheese_total = 18;
			start_game(app, 3);
		}
		ImGui::SameLine();
		if (ImGui::Button("100", ImVec2(ui(52), 0))) {
			app.cheese_total = 100;
			start_game(app, 3);
		}
		ImGui::TextDisabled("Dig that many rows; the clock stops at the last.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Cheese survival");
		ImGui::SameLine();
		if (ImGui::Button("8s", ImVec2(ui(52), 0))) {
			app.cheese_period = 400;
			start_game(app, 4);
		}
		ImGui::SameLine();
		if (ImGui::Button("5s", ImVec2(ui(52), 0))) {
			app.cheese_period = 250;
			start_game(app, 4);
		}
		ImGui::SameLine();
		if (ImGui::Button("3s", ImVec2(ui(52), 0))) {
			app.cheese_period = 150;
			start_game(app, 4);
		}
		ImGui::TextDisabled("The floor rises on that clock. Outlast it.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Holes per row");
		for (int holes = 1; holes <= 3; ++holes) {
			ImGui::SameLine();
			char label[4];
			std::snprintf(label, sizeof label, "%d", holes);
			if (option_button(label, app.cheese_holes == holes, ui(44))) {
				app.cheese_holes = holes;
			}
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Messiness");
		static const struct { const char* label; int percent; } kMess[] = {
			{"Clean", 0}, {"Low", 33}, {"High", 66}, {"Full", 100},
		};
		for (const auto& mess : kMess) {
			ImGui::SameLine();
			if (option_button(mess.label, app.cheese_messiness == mess.percent,
				ui(62))) {
				app.cheese_messiness = mess.percent;
			}
		}
		ImGui::TextDisabled("How the cheese is cut: holes per row, and how");
		ImGui::TextDisabled("often they move between rows.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Versus");
		const auto& ladder = bot::ranks();
		for (size_t i = 0; i < ladder.size(); ++i) {
			ImGui::SameLine();
			if (option_button(ladder[i].name,
				app.bot_rank == static_cast<int>(i), ui(34))) {
				app.bot_rank = static_cast<int>(i);
			}
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("First to");
		for (int ft = 1; ft <= 3; ++ft) {
			ImGui::SameLine();
			char label[8];
			std::snprintf(label, sizeof label, "FT%d", ft);
			if (option_button(label, app.first_to == ft, ui(52))) {
				app.first_to = ft;
			}
		}
		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ui(8), 0.f));
		ImGui::SameLine();
		if (ImGui::Button("Fight", ImVec2(ui(90), 0))) {
			start_versus(app);
		}
		ImGui::TextDisabled("A bot paced at that rank's league-average PPS,");
		ImGui::TextDisabled("garbage, cancelling and surge included.");
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("Back", ImVec2(ui(280), 0))) {
			app.screen = Screen::Menu;
		}
		ImGui::End();
		ImGui::PopStyleVar();
	} else if (app.screen == Screen::Game && app.paused) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("paused", nullptr, box);
		ImGui::TextUnformatted("Paused");
		ImGui::Spacing();
		if (ImGui::Button("Resume", ImVec2(ui(240), 0))) {
			app.paused = false;
		}
		if (ImGui::Button("Restart", ImVec2(ui(240), 0))) {
			if (app.mode == 5) {
				start_versus(app);
			} else {
				start_game(app, app.mode);
			}
		}
		if (ImGui::Button("Edit stat layout", ImVec2(ui(240), 0))) {
			open_layout_editor(app);
		}
		if (ImGui::Button("Settings", ImVec2(ui(240), 0))) {
			app.show_settings = true;
		}
		if (ImGui::Button("Back to menu", ImVec2(ui(240), 0))) {
			app.versus.reset();
			app.screen = Screen::Menu;
		}
		ImGui::End();
	} else if (app.screen == Screen::Over) {
		ImGui::SetNextWindowPos(ImVec2(middle.x, ui(6)),
			ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(6)));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui(8), ui(3)));
		ImGui::Begin("game over", nullptr, box);
		const bool won = app.session.has_value() && app.session->sim().won();
		ImGui::PushFont(app.fonts.head);
		if (app.versus.has_value()) {
			ImGui::TextUnformatted(
				app.versus->player_wins > app.versus->bot_wins
					? "You win the match!" : "You lose the match");
		} else {
			ImGui::TextUnformatted(won ? "Finished!" : "Game over");
		}
		ImGui::PopFont();
		if (app.versus.has_value()) {
			ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
				"You %d - %d Bot (%s)  first to %d",
				app.versus->player_wins, app.versus->bot_wins,
				bot::ranks()[app.versus->rank_index].name,
				app.versus->first_to);
		}
		if (won) {
			const double seconds = app.session->sim().frame() * 0.02;
			ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
				"All the cheese in %d:%05.2f",
				static_cast<int>(seconds) / 60, std::fmod(seconds, 60.));
		}
		ImGui::Spacing();
		// What the run was made of, as the Python analysis screen lists it -
		// which needs the recording. A game too short for a file says so and
		// falls back to the totals the panels were already showing.
		if (app.last_replay.has_value()) {
			draw_analysis_rows(*app.last_replay);
		} else if (app.session.has_value()) {
			ImGui::TextDisabled("That run was too short to record.");
			draw_summary(*app.session);
		}
		ImGui::Spacing();
		if (app.hiscore_place >= 0 && !app.score_saved) {
			ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f),
				"You got the %s place high score!",
				place_string(app.hiscore_place).c_str());
			ImGui::SetNextItemWidth(ui(150));
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
			if (ImGui::Button("Full analysis", ImVec2(ui(240), 0))) {
				app.studying = app.last_replay;
				app.study_back = Screen::Over;
				app.screen = Screen::Analysis;
			}
			if (ImGui::Button("Watch replay", ImVec2(ui(240), 0))) {
				watch(app, *app.last_replay, Screen::Over);
			}
		} else {
			ImGui::TextDisabled("Too short to record.");
		}
		if (ImGui::Button("Play again", ImVec2(ui(240), 0))) {
			if (app.mode == 5) {
				start_versus(app);
			} else {
				start_game(app, app.mode);
			}
		}
		if (ImGui::Button("Back to menu", ImVec2(ui(240), 0))) {
			app.versus.reset();
			app.screen = Screen::Menu;
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	if (app.show_settings) {
		draw_settings(app);
	}
}

// --- The loop. -------------------------------------------------------------

// The screens the smoke run visits between games, in order. Each is opened
// the way a player opens it - with the data the screen reads actually
// loaded - so the headless run proves they draw, not merely that they build.
// A picture of the frame as drawn, before the present wipes it.
void capture_frame (SDL_Renderer* renderer, const std::string& path) {
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(renderer, &w, &h);
	SDL_Surface* grab = SDL_CreateRGBSurfaceWithFormat(
		0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (grab != nullptr && SDL_RenderReadPixels(renderer, nullptr,
		grab->format->format, grab->pixels, grab->pitch) == 0) {
		SDL_SaveBMP(grab, path.c_str());
	}
	SDL_FreeSurface(grab);
}

// Which screen the frame shows, for the one-shot-per-screen gallery
// FORCETRIS_SHOTS collects - the way a design change is looked at without a
// display, one BMP per screen the run visits.
std::string screen_shot_key (const App& app) {
	if (app.editing) {
		return "editor";
	}
	if (app.show_settings) {
		return "settings";
	}
	switch (app.screen) {
		case Screen::Menu: return "menu";
		case Screen::Modes: return "modes";
		case Screen::Game:
			return app.paused ? "pause"
				: app.versus.has_value() ? "versus" : "game";
		case Screen::Over: return "over";
		case Screen::Replays: return "replays";
		case Screen::Viewer: return "viewer";
		case Screen::Scores: return "scores";
		case Screen::Help: return "help";
		case Screen::Analysis:
			return app.studying.has_value() ? "analysis" : "analysis_empty";
	}
	return "screen";
}

constexpr int kTour = 12;

void tour_screen (App& app, int stop) {
	switch (stop) {
		case 0:
			app.help_back = Screen::Over;
			app.screen = Screen::Help;
			break;
		case 1:
			app.score_page = 0;
			app.screen = Screen::Scores;
			break;
		case 2:
			app.score_page = 2;
			app.screen = Screen::Scores;
			break;
		case 3:
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
			break;
		case 4:
			// The analysis of the run just finished, or of the newest file
			// on the shelf if this game was too short to keep.
			app.studying = app.last_replay.has_value()
				? app.last_replay
				: (app.shelf.empty() ? std::nullopt
					: std::optional<replay::Replay>(app.shelf.front()));
			app.study_back = Screen::Over;
			app.screen = Screen::Analysis;
			break;
		case 5:
			// And the same screen with nothing to show, which is what a run
			// too short to record leaves it holding.
			app.studying.reset();
			app.study_back = Screen::Over;
			app.screen = Screen::Analysis;
			break;
		case 6:
			app.screen = Screen::Menu;
			app.show_settings = true;
			break;
		case 7:
			// The layout editor over the finished game's board.
			app.screen = Screen::Game;
			app.editing = true;
			app.place_panels = true;
			break;
		case 8:
			app.screen = Screen::Game;
			app.paused = true;
			break;
		case 9:
			// The editor as the settings screen opens it: over a throwaway
			// preview board, no game running.
			app.session.reset();
			app.screen = Screen::Menu;
			open_layout_editor(app);
			break;
		case 10:
			app.screen = Screen::Modes;
			break;
		default:
			app.screen = Screen::Menu;
			break;
	}
}

int run (bool smoke, long smoke_frames) {
	App app;
	app.config_file = config_path();
	app.config = load_config(app.config_file);

#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
	// Without this Windows draws the window at 96dpi and stretches the
	// bitmap to the display's scale, which blurs every pixel in it. With it
	// the window is laid out in real pixels and the scale below re-derives
	// the same design at the display's density.
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
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
	float scale = 1.f;
	float dpi = 0.f;
	if (SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) == 0 && dpi > 0.f) {
		scale = std::clamp(dpi / 96.f, 1.f, 3.f);
	}
	// The escape hatch for displays that misreport their density - WSLg says
	// 96dpi while Windows stretches the result - and for anyone who simply
	// wants the whole game bigger.
	if (const char* forced = std::getenv("FORCETRIS_SCALE")) {
		const float wanted = static_cast<float>(std::atof(forced));
		if (wanted > 0.f) {
			scale = std::clamp(wanted, 1.f, 3.f);
		}
	}
	apply_ui_scale(scale);
	app.window = SDL_CreateWindow("Forcetris",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, px(1180), px(700),
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
	apply_theme();
	app.fonts = load_fonts();
	ImGui_ImplSDL2_InitForSDLRenderer(app.window, app.renderer);
	ImGui_ImplSDLRenderer2_Init(app.renderer);

	std::mt19937 mash(20260818);
	if (smoke) {
		// FORCETRIS_SMOKE_MODE picks which mode the scripted run plays, so
		// every mode's whole loop - dealer, session, end screen - can be
		// proven headlessly, not only free's.
		int mode = 0;
		if (const char* forced = std::getenv("FORCETRIS_SMOKE_MODE")) {
			mode = std::clamp(std::atoi(forced), 0, 5);
		}
		if (mode == 4) {
			app.cheese_period = 150;
		}
		if (mode == 5) {
			start_versus(app);
		} else {
			start_game(app, mode);
		}
	}

	Uint64 previous = SDL_GetPerformanceCounter();
	double behind = 0.;
	long frames = 0;
	// Where the per-screen gallery goes, if anywhere.
	const char* shots_dir = std::getenv("FORCETRIS_SHOTS");
	std::map<std::string, int> shot_frames;
	if (shots_dir != nullptr) {
		std::error_code ignored;
		std::filesystem::create_directories(shots_dir, ignored);
	}

	// The smoke run's tour of the screens a game never opens. It only starts
	// once a game has ended, and the viewer mode skips it, so the run is only
	// held to finishing a tour it was in a position to start.
	const bool touring = smoke && std::getenv("FORCETRIS_SMOKE_VIEW") == nullptr;
	int toured = 0;
	int tour_frames = 0;
	bool game_ended = false;
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
				const bool live = app.session->step();
				if (app.versus.has_value()) {
					app.versus->step(*app.session);
					// The round and match flow: linger on the verdict, then
					// the next round or the loss screen.
					if (app.versus->phase == VersusMatch::Phase::RoundOver) {
						if (++app.versus->phase_frames >= 150) {
							next_versus_round(app);
						}
					} else if (app.versus->phase
						== VersusMatch::Phase::MatchOver) {
						if (++app.versus->phase_frames >= 100) {
							end_game(app);
						}
					}
				} else if (!live) {
					end_game(app);
				}
				++frames;
			} else if (app.screen == Screen::Viewer && app.viewing.has_value()) {
				advance_viewer(app);
				if (smoke) {
					++frames;
				}
			} else if (smoke) {
				// A menu screen on the tour still counts, so the run always
				// reaches its frame budget rather than sitting on one.
				++frames;
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
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			draw_stat_panels(app);
			draw_banner(app);
			if (app.versus.has_value()) {
				draw_versus_panel(app);
			}
		}
		if (app.screen == Screen::Viewer && app.viewing.has_value()) {
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			draw_viewer(app);
		}
		if (app.screen == Screen::Replays) {
			draw_replays(app);
		}
		if (app.screen == Screen::Scores) {
			draw_scores(app);
		}
		if (app.screen == Screen::Analysis) {
			draw_analysis(app);
		}
		if (app.screen == Screen::Help) {
			draw_help(app);
		}
		if (app.editing) {
			draw_layout_editor(app);
		}
		if (app.layout_preview && !app.editing) {
			// The throwaway preview board goes with the editor, back to the
			// settings screen it was opened from.
			app.layout_preview = false;
			app.session.reset();
			app.screen = Screen::Menu;
			app.show_settings = true;
		}
		draw_menus(app);

		ImGui::Render();
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);

		if (smoke && frames >= smoke_frames) {
			// A picture of the last frame, before the present wipes it, so a
			// headless run can be looked at rather than taken on faith.
			if (const char* shot = std::getenv("FORCETRIS_SHOT")) {
				capture_frame(app.renderer, shot);
			}
		}
		if (shots_dir != nullptr) {
			// One picture per screen, taken a few frames in so the layout
			// has settled.
			const std::string key = screen_shot_key(app);
			if (++shot_frames[key] == 4) {
				capture_frame(app.renderer,
					(std::filesystem::path(shots_dir) / (key + ".bmp")).string());
			}
		}
		SDL_RenderPresent(app.renderer);

		if (smoke) {
			if (app.screen == Screen::Over) {
				game_ended = true;
				// With FORCETRIS_SMOKE_VIEW set the run ends in the replay
				// viewer instead of another game, so the screenshot shows a
				// recording being re-enacted.
				if (!touring && app.last_replay.has_value()) {
					watch(app, *app.last_replay, Screen::Menu);
				} else if (toured < kTour) {
					// Between games, walk the screens a game never opens, a
					// few frames each: every one of them has to build and
					// draw with real data behind it, not merely compile.
					tour_screen(app, toured++);
				} else if (app.mode == 5) {
					start_versus(app);
				} else {
					start_game(app, app.mode);
				}
			} else if (toured > 0 && toured <= kTour
				&& app.screen != Screen::Viewer
				&& (app.screen != Screen::Game || app.paused || app.editing)) {
				if (++tour_frames >= 6) {
					tour_frames = 0;
					app.screen = Screen::Over;
					app.studying.reset();
					app.show_settings = false;
					app.editing = false;
					app.paused = false;
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
		SDL_Log("smoke: ran %ld frames, toured %d of %d screens",
			frames, toured, kTour);
		if (touring && game_ended && toured < kTour) {
			// The tour is the only thing that opens the menu screens, so a
			// run that had the chance to walk them and did not finish proves
			// nothing about them. A budget too short to end a single game is
			// not held to it - there was never a screen to open.
			SDL_Log("smoke: the screen tour did not finish");
			return 1;
		}
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
