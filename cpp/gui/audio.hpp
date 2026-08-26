// Sound: the forge, heard.
//
// Two halves. The one-shot cues in sound/ are synthesised WAVs written by
// tools/make_sounds.py and fired by name from the sim's cue stream, which the
// trace harness grades - so what fires and when is the engine's decision and
// this only makes it audible.
//
// The other half is generated here, sample by sample, in the mixer: the room
// the game is played in. A furnace roar whose level follows the same heat and
// danger the backdrop draws with, and the Forge score - a small sequencer
// whose layers arrive as the board gets hotter, so a run that goes well ends
// up with more music playing than a run that does not. Nothing on disk, no
// loop seam, and the intensity is a parameter rather than a mix decision.
//
// The classic chiptune track is still there under Music::Classic, decoded
// from music/tetris.ogg, for anyone who wants the trainer's voice back.
//
// Mixing is done in the SDL callback: the bed, the score, and a pool of
// one-shot voices, each with its own volume, the music with a fade the game
// over screen uses. A machine with no audio device (or the dummy driver)
// simply leaves the mixer closed and every call becomes a no-op.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

namespace forcetris {
namespace gui {

class Audio {
public:
	// What plays behind the game.
	enum class Music {
		Forge = 0,    // The generated score, which grows with the board.
		Classic = 1,  // music/tetris.ogg, the way the trainer had it.
		Off = 2,
	};

	~Audio ();

	// Open the device and load everything under `root` (the repository
	// checkout: sound/*.wav and music/tetris.ogg). Safe to call on a machine
	// with neither; the mixer just stays silent.
	//
	// `play` false opens and loads but leaves the device paused, so the only
	// audio produced is what a caller pulls out of mix() itself. That is what
	// audiocheck renders with - the driver's own thread taking buffers on its
	// own schedule would make every measurement a race.
	void open (const std::string& root, bool play = true);

	// Fire a cue by the name the sim uses. Unknown or unloaded names are
	// ignored, the way the Python game shrugs off a missing file.
	void play (const std::string& cue);

	void set_sfx_volume (float volume);
	void set_music_volume (float volume);
	void set_music_mode (Music mode);
	void set_ambience (bool on);

	// How hot the room is, from the same readings the backdrop uses: the
	// Flow gauge, how much trouble the board is in, whether Overdrive is
	// lit, and whether a game is being played at all. Everything the mixer
	// generates leans on these, and they are smoothed per sample, so this
	// is safe to call every frame.
	void set_room (float heat, float danger, bool burning, bool playing);

	// Start the music from the top, at full configured volume.
	void start_music ();
	// Fade it out over a stretch of seconds, as the game over screen does.
	void fade_music (double seconds);
	// Playback speed, 1.0 for the plain track - Overdrive runs it hot. On the
	// Forge score this is tempo and brightness rather than pitch.
	void set_music_rate (float rate);

	bool ready () const { return device_ != 0; }

	// The mixer, public only for the C callback - and for audiocheck, which
	// renders buffers without a device to grade what comes out.
	void mix (float* out, int samples);

private:
	// A struck voice in the score: three inharmonic partials, each decaying
	// at its own rate, which is what separates metal from an organ.
	struct Struck {
		bool on = false;
		double phase[3] = {0., 0., 0.};
		double step[3] = {0., 0., 0.};
		float amp[3] = {0.f, 0.f, 0.f};
		float decay[3] = {0.f, 0.f, 0.f};
		float pan = 0.5f;
	};

	struct Voice {
		const std::vector<float>* data = nullptr;
		size_t at = 0;
	};

	// Deterministic white noise: the same room every run, and the same
	// buffers under audiocheck.
	float noise ();
	// One sine from the table, phase in turns.
	static float wave (double phase);
	// Fire a struck note into the pool.
	void strike (double freq, float amp, float decay, float pan, bool bell);
	// Advance the sequencer by one beat and trigger whatever that beat holds.
	void beat (int index);
	// Render `samples` of the bed and the score, added into `out`.
	void render_room (float* out, int samples);

	SDL_AudioDeviceID device_ = 0;
	std::map<std::string, std::vector<float>> effects_;
	std::vector<float> music_;
	Voice voices_[24];
	bool music_on_ = false;
	Music music_mode_ = Music::Forge;
	bool ambience_ = true;
	float music_rate_ = 1.f;     // Playback speed; Overdrive leans on it.
	double music_pos_ = 0.;      // Fractional frame position for the rate.
	float sfx_volume_ = 1.f;
	float music_volume_ = 1.f;
	float fade_ = 1.f;
	float fade_step_ = 0.f;

	// --- The room ---------------------------------------------------------
	float want_heat_ = 0.f;      // Targets, set from the game thread...
	float want_danger_ = 0.f;
	float want_burn_ = 0.f;
	float want_play_ = 0.f;
	float heat_ = 0.f;           // ...and the smoothed values the mixer uses.
	float danger_ = 0.f;
	float burn_ = 0.f;
	float play_ = 0.f;

	uint32_t seed_ = 0x9e3779b9u;
	float roar_lo_ = 0.f;        // Two-pole state for the furnace noise.
	float roar_lo2_ = 0.f;
	float roar_air_ = 0.f;
	double roar_lfo_ = 0.;

	double drone_phase_[3] = {0., 0., 0.};
	float drone_lp_ = 0.f;
	double sub_phase_ = 0.;

	double beat_pos_ = 0.;       // Beats since the score started.
	int beat_last_ = -1;
	Struck struck_[16];

	std::vector<float> delay_;   // One stereo hall for the score.
	size_t delay_at_ = 0;
	float delay_lp_[2] = {0.f, 0.f};
};

} // namespace gui
} // namespace forcetris
