// The mixer: what comes out of it, graded rather than guessed at.
//
// Half of the game's audio is now generated in the callback - a furnace bed
// and the Forge score - which is exactly the sort of thing that goes wrong
// quietly. So this renders buffers straight out of Audio::mix and measures
// them: silence when silence was asked for, a room that gets louder as the
// board gets hotter, nothing that clips or goes non-finite, a cue pool that
// steals rather than drops, and the same bytes twice from the same start.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL.h>

#include "../gui/audio.hpp"

using forcetris::gui::Audio;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr int kBuffer = 256 * kChannels;

// Render `seconds` of output and hand back every sample, so a test can ask
// whatever it likes of them.
std::vector<float> render (Audio& audio, double seconds) {
	std::vector<float> all;
	std::vector<float> buffer(kBuffer);
	const int rounds = static_cast<int>(seconds * kRate / (kBuffer / kChannels));
	for (int i = 0; i < rounds; ++i) {
		audio.mix(buffer.data(), kBuffer);
		all.insert(all.end(), buffer.begin(), buffer.end());
	}
	return all;
}

double rms (const std::vector<float>& samples) {
	if (samples.empty()) {
		return 0.;
	}
	double sum = 0.;
	for (const float sample : samples) {
		sum += static_cast<double>(sample) * sample;
	}
	return std::sqrt(sum / samples.size());
}

bool sane (const std::vector<float>& samples) {
	for (const float sample : samples) {
		if (!std::isfinite(sample) || sample < -1.f || sample > 1.f) {
			return false;
		}
	}
	return true;
}

std::string number (double value) {
	char text[64];
	std::snprintf(text, sizeof text, "%.5f", value);
	return text;
}

} // namespace

int main (int argc, char** argv) {
	// The dummy driver still runs a thread that pulls buffers on its own
	// schedule, which would make every measurement here a race - so every
	// Audio below is opened paused, and the only buffers that exist are the
	// ones this test asks for.
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	if (SDL_Init(SDL_INIT_AUDIO) != 0) {
		std::printf("FAIL could not start SDL audio -- %s\n", SDL_GetError());
		return 1;
	}
	const std::string root = argc > 1 ? argv[1] : ".";

	// --- Silence is available. ----------------------------------------------
	{
		Audio audio;
		audio.open(root, false);
		audio.set_ambience(false);
		audio.set_music_mode(Audio::Music::Off);
		const std::vector<float> quiet = render(audio, 0.2);
		check("with the room off and no music, nothing is generated",
			rms(quiet) == 0., number(rms(quiet)));
	}

	// --- The room idles, then answers the board. ----------------------------
	{
		Audio audio;
		audio.open(root, false);
		audio.set_music_mode(Audio::Music::Off);
		audio.set_room(0.f, 0.f, false, false);
		const std::vector<float> idle = render(audio, 2.);
		check("an idle room hums rather than roars",
			rms(idle) > 0. && rms(idle) < 0.05, number(rms(idle)));

		// Each step is rendered long enough for the smoothing to arrive.
		audio.set_room(0.f, 0.f, false, true);
		const double playing = rms(render(audio, 3.));
		audio.set_room(1.f, 0.4f, false, true);
		const double hot = rms(render(audio, 3.));
		audio.set_room(1.f, 0.8f, true, true);
		const double lit = rms(render(audio, 3.));
		check("the room gets louder as the board heats up",
			playing > rms(idle) && hot > playing && lit > hot,
			number(rms(idle)) + " < " + number(playing) + " < " + number(hot)
				+ " < " + number(lit));
	}

	// --- The score plays, and grows with the heat. --------------------------
	{
		Audio audio;
		audio.open(root, false);
		audio.set_ambience(false);
		audio.set_music_mode(Audio::Music::Forge);
		const double before = rms(render(audio, 0.5));
		check("no score until the game starts it", before == 0., number(before));

		audio.set_room(0.f, 0.f, false, true);
		audio.start_music();
		const double cold = rms(render(audio, 6.));
		audio.set_room(1.f, 0.f, true, true);
		const double blazing = rms(render(audio, 6.));
		check("the Forge score plays", cold > 0., number(cold));
		check("its layers arrive as the board heats up", blazing > cold,
			number(cold) + " -> " + number(blazing));

		audio.fade_music(0.5);
		const std::vector<float> faded = render(audio, 2.);
		// The last quarter second is after the fade has finished.
		const std::vector<float> tail(faded.end() - kBuffer * 40, faded.end());
		check("a fade takes the score with it", rms(tail) == 0., number(rms(tail)));
	}

	// --- The classic track still plays. -------------------------------------
	{
		Audio audio;
		audio.open(root, false);
		audio.set_ambience(false);
		audio.set_music_mode(Audio::Music::Classic);
		audio.start_music();
		const double classic = rms(render(audio, 1.));
		check("Music::Classic still plays the chiptune track", classic > 0.,
			number(classic));
	}

	// --- Nothing tears. -----------------------------------------------------
	{
		Audio audio;
		audio.open(root, false);
		audio.set_music_mode(Audio::Music::Forge);
		audio.set_room(1.f, 1.f, true, true);
		audio.start_music();
		// Everything at once: the room lit, the score full, and the pool
		// stuffed with the longest cues there are.
		for (int i = 0; i < 40; ++i) {
			audio.play("overdrive");
			audio.play("pressure");
			audio.play("tetris");
		}
		const std::vector<float> loud = render(audio, 3.);
		check("the loudest thing the mixer can do stays in range and finite",
			sane(loud), number(rms(loud)));
	}

	// --- A full pool steals instead of dropping. ----------------------------
	{
		// The same scenario twice, once with an extra cue fired into a pool
		// that is already full. If the pool dropped it, the two runs would
		// be identical; that it steals is exactly what makes them differ.
		auto run = [&root] (bool extra) {
			Audio audio;
			audio.open(root, false);
			audio.set_ambience(false);
			audio.set_music_mode(Audio::Music::Off);
			// Quiet enough that two dozen overlapping tails never reach the
			// clamp, so a small cue on top is still visible in the sum.
			audio.set_sfx_volume(0.02f);
			for (int i = 0; i < 30; ++i) {
				audio.play("pressure");
			}
			render(audio, 0.1);
			if (extra) {
				audio.play("move");
			}
			return render(audio, 0.05);
		};
		const std::vector<float> without = run(false);
		const std::vector<float> with = run(true);
		bool differs = false;
		for (size_t i = 0; i < with.size() && !differs; ++i) {
			differs = with[i] != without[i];
		}
		check("a cue fired into a full pool still sounds", differs);
	}

	// --- The same start gives the same buffers. -----------------------------
	{
		auto run = [&root] {
			Audio audio;
			audio.open(root, false);
			audio.set_music_mode(Audio::Music::Forge);
			audio.set_room(0.6f, 0.3f, false, true);
			audio.start_music();
			audio.play("clear");
			return render(audio, 1.5);
		};
		const std::vector<float> first = run();
		const std::vector<float> second = run();
		check("two runs from the same start render the same samples",
			first == second);
	}

	SDL_Quit();
	std::printf("%s\n", failures == 0 ? "all audio checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
