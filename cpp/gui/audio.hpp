// Sound: the same files the Python game plays, through SDL's own device.
//
// The effects in sound/ are synthesised WAVs written by tools/make_sounds.py
// and the music is music/tetris.ogg, so nothing here invents audio - the two
// games share their voice. Cues arrive by name from the sim's cue stream,
// which the trace harness grades, so what fires and when is the engine's
// decision; this only makes it audible.
//
// Mixing is done in the SDL callback: a handful of one-shot voices over a
// looping music track, each with its own volume, the music with a fade the
// game-over screen uses. A machine with no audio device (or the dummy
// driver) simply leaves the mixer closed and every call becomes a no-op.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <SDL.h>

namespace forcetris {
namespace gui {

class Audio {
public:
	~Audio ();

	// Open the device and load everything under `root` (the repository
	// checkout: sound/*.wav and music/tetris.ogg). Safe to call on a machine
	// with neither; the mixer just stays silent.
	void open (const std::string& root);

	// Fire a cue by the name the sim uses. Unknown or unloaded names are
	// ignored, the way the Python game shrugs off a missing file.
	void play (const std::string& cue);

	void set_sfx_volume (float volume);
	void set_music_volume (float volume);

	// Start the music from the top, at full configured volume.
	void start_music ();
	// Fade it out over a stretch of seconds, as the game over screen does.
	void fade_music (double seconds);

	bool ready () const { return device_ != 0; }

	// The mixer, public only for the C callback.
	void mix (float* out, int samples);

private:
	struct Voice {
		const std::vector<float>* data = nullptr;
		size_t at = 0;
	};

	SDL_AudioDeviceID device_ = 0;
	std::map<std::string, std::vector<float>> effects_;
	std::vector<float> music_;
	Voice voices_[12];
	size_t music_at_ = 0;
	bool music_on_ = false;
	float sfx_volume_ = 1.f;
	float music_volume_ = 1.f;
	float fade_ = 1.f;
	float fade_step_ = 0.f;
};

} // namespace gui
} // namespace forcetris
