#include "audio.hpp"

#include <algorithm>
#include <cmath>

#include "stb_vorbis.c"

namespace forcetris {
namespace gui {

namespace {

// The device format everything is converted into once, at load time.
constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr double kTau = 6.283185307179586;

// The cue names, matching engine/environment.py's SFX_NAMES.
std::vector<std::string> cue_names () {
	std::vector<std::string> names = {
		"move", "rotate", "hold", "lock", "drop", "forced", "clear", "tetris",
		"tspin", "perfect", "b2b", "finesse", "gameover",
		"fusewarn", "flash", "overdrive", "overdrive_end", "burn", "pressure", "hit",
		"cascade",
	};
	for (int step = 1; step <= 10; ++step) {
		names.push_back("combo" + std::to_string(step));
	}
	return names;
}

// --- The Forge score --------------------------------------------------------
// Eight bars in D minor, two chords a bar apart, slow enough that the loop
// reads as a room rather than as a tune. The layers on top of it are gated by
// how hot the board is, so the score is quiet while you are safe and full
// while you are not - the music is a readout, not a backing track.

constexpr int kBeatsPerBar = 4;
constexpr int kBars = 8;
constexpr int kBeats = kBeatsPerBar * kBars;
constexpr double kTempo = 88.;   // BPM at rest; Overdrive pushes it.

// One root per bar. D2, then down to Bb1 and back up through F2 and C2.
constexpr double kRoots[kBars] = {73.42, 73.42, 58.27, 65.41, 73.42, 73.42, 87.31, 65.41};

// The lead figure, in semitones above the bar's root; -1 is a rest. It is
// mostly rests on purpose - a sparse line over a drone sounds deliberate,
// and a busy one would fight the cues.
constexpr int kLead[kBeats] = {
	-1, -1, 12, -1,   -1, 10, -1, -1,   15, -1, -1, 12,   -1, -1, 10, -1,
	-1, -1, 19, -1,   -1, 15, -1, -1,   17, -1, 12, -1,   -1, 10, -1, -1,
};

// Chowning's bell ratios and the denser anvil set, the same two the effect
// synthesiser strikes with, so the score and the cues are the same metal.
constexpr double kBell[3] = {1., 2.76, 5.40};
constexpr double kAnvil[3] = {1., 2.32, 3.83};

// A quarter of a second of hall, tapped twice.
constexpr size_t kDelayFrames = 17640;
constexpr size_t kTapLeft = 13230;
constexpr size_t kTapRight = 16317;

const float* sine_table () {
	constexpr size_t kSize = 4096;
	static const std::vector<float> table = [] {
		std::vector<float> built(kSize + 1);
		for (size_t i = 0; i <= kSize; ++i) {
			built[i] = static_cast<float>(std::sin(kTau * i / kSize));
		}
		return built;
	}();
	return table.data();
}

void mix_callback (void* self, Uint8* stream, int bytes) {
	static_cast<Audio*>(self)->mix(
		reinterpret_cast<float*>(stream), bytes / static_cast<int>(sizeof(float)));
}

} // namespace

Audio::~Audio () {
	if (device_ != 0) {
		SDL_CloseAudioDevice(device_);
	}
}

void Audio::open (const std::string& root, bool play) {
	SDL_AudioSpec want{};
	want.freq = kRate;
	want.format = AUDIO_F32SYS;
	want.channels = kChannels;
	// A small buffer: the keypress cues are half the feel of the game, and
	// 1024 samples was 23ms of extra lag on every one of them. The mixer
	// only sums a couple of dozen voices, so 256 (about 6ms) is safe; a
	// device that cannot do it hands back what it can in `got`.
	want.samples = 256;
	want.callback = mix_callback;
	want.userdata = this;
	SDL_AudioSpec got{};
	device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
	// The hall is allocated once, here, and the sine table built once, so
	// the callback never has to do either.
	delay_.assign(kDelayFrames * kChannels, 0.f);
	sine_table();
	if (device_ == 0) {
		return;
	}

	// The effects: WAVs, mono or stereo, converted to the device format. A
	// file that is missing or unreadable is simply left out.
	for (const std::string& name : cue_names()) {
		const std::string path = root + "/sound/" + name + ".wav";
		SDL_AudioSpec spec{};
		Uint8* data = nullptr;
		Uint32 length = 0;
		if (SDL_LoadWAV(path.c_str(), &spec, &data, &length) == nullptr) {
			continue;
		}
		SDL_AudioCVT cvt{};
		if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
			AUDIO_F32SYS, kChannels, kRate) >= 0) {
			std::vector<Uint8> buffer(length * std::max(1, cvt.len_mult));
			std::copy(data, data + length, buffer.begin());
			cvt.buf = buffer.data();
			cvt.len = static_cast<int>(length);
			if (SDL_ConvertAudio(&cvt) == 0) {
				const float* samples = reinterpret_cast<const float*>(buffer.data());
				effects_[name].assign(
					samples, samples + cvt.len_cvt / sizeof(float));
			}
		}
		SDL_FreeWAV(data);
	}

	// The classic track, decoded whole. It is half a minute of chiptune, not
	// an album, and only Music::Classic ever reads it.
	int channels = 0;
	int rate = 0;
	short* decoded = nullptr;
	const int frames = stb_vorbis_decode_filename(
		(root + "/music/tetris.ogg").c_str(), &channels, &rate, &decoded);
	if (frames > 0 && decoded != nullptr) {
		SDL_AudioCVT cvt{};
		if (SDL_BuildAudioCVT(&cvt, AUDIO_S16SYS, static_cast<Uint8>(channels),
			rate, AUDIO_F32SYS, kChannels, kRate) >= 0) {
			const size_t bytes = static_cast<size_t>(frames) * channels * sizeof(short);
			std::vector<Uint8> buffer(bytes * std::max(1, cvt.len_mult));
			std::copy(reinterpret_cast<Uint8*>(decoded),
				reinterpret_cast<Uint8*>(decoded) + bytes, buffer.begin());
			cvt.buf = buffer.data();
			cvt.len = static_cast<int>(bytes);
			if (SDL_ConvertAudio(&cvt) == 0) {
				const float* samples = reinterpret_cast<const float*>(buffer.data());
				music_.assign(samples, samples + cvt.len_cvt / sizeof(float));
			}
		}
	}
	if (decoded != nullptr) {
		free(decoded);
	}

	if (play) {
		SDL_PauseAudioDevice(device_, 0);
	}
}

void Audio::play (const std::string& cue) {
	if (device_ == 0 || sfx_volume_ <= 0.f) {
		return;
	}
	const auto found = effects_.find(cue);
	if (found == effects_.end()) {
		return;
	}
	SDL_LockAudioDevice(device_);
	Voice* pick = nullptr;
	double furthest = -1.;
	for (Voice& voice : voices_) {
		if (voice.data == nullptr) {
			pick = &voice;
			break;
		}
		// Nothing free: take the voice nearest the end of its own sound.
		// The cues have tails now, and a pool that drops instead of steals
		// would swallow a move tick under a ringing tetris - which reads as
		// a missed keypress, not as a busy mixer.
		const double done = static_cast<double>(voice.at)
			/ static_cast<double>(std::max<size_t>(1, voice.data->size()));
		if (done > furthest) {
			furthest = done;
			pick = &voice;
		}
	}
	if (pick != nullptr) {
		pick->data = &found->second;
		pick->at = 0;
	}
	SDL_UnlockAudioDevice(device_);
}

void Audio::set_sfx_volume (float volume) {
	sfx_volume_ = std::clamp(volume, 0.f, 1.f);
}

void Audio::set_music_volume (float volume) {
	if (device_ != 0) {
		SDL_LockAudioDevice(device_);
	}
	music_volume_ = std::clamp(volume, 0.f, 1.f);
	if (device_ != 0) {
		SDL_UnlockAudioDevice(device_);
	}
}

void Audio::set_music_mode (Music mode) {
	if (device_ != 0) {
		SDL_LockAudioDevice(device_);
	}
	music_mode_ = mode;
	if (device_ != 0) {
		SDL_UnlockAudioDevice(device_);
	}
}

void Audio::set_ambience (bool on) {
	if (device_ != 0) {
		SDL_LockAudioDevice(device_);
	}
	ambience_ = on;
	if (device_ != 0) {
		SDL_UnlockAudioDevice(device_);
	}
}

void Audio::set_room (float heat, float danger, bool burning, bool playing) {
	if (device_ != 0) {
		SDL_LockAudioDevice(device_);
	}
	want_heat_ = std::clamp(heat, 0.f, 1.f);
	want_danger_ = std::clamp(danger, 0.f, 1.f);
	want_burn_ = burning ? 1.f : 0.f;
	want_play_ = playing ? 1.f : 0.f;
	if (device_ != 0) {
		SDL_UnlockAudioDevice(device_);
	}
}

void Audio::set_music_rate (float rate) {
	if (device_ != 0) {
		SDL_LockAudioDevice(device_);
	}
	music_rate_ = std::clamp(rate, 0.5f, 2.f);
	if (device_ != 0) {
		SDL_UnlockAudioDevice(device_);
	}
}

void Audio::start_music () {
	if (device_ == 0) {
		return;
	}
	SDL_LockAudioDevice(device_);
	music_pos_ = 0.;
	music_on_ = true;
	fade_ = 1.f;
	fade_step_ = 0.f;
	// The score restarts from the top of the loop too, so every game opens
	// on the downbeat rather than wherever the last one left off.
	beat_pos_ = 0.;
	beat_last_ = -1;
	for (Struck& note : struck_) {
		note.on = false;
	}
	SDL_UnlockAudioDevice(device_);
}

void Audio::fade_music (double seconds) {
	if (device_ == 0 || !music_on_) {
		return;
	}
	SDL_LockAudioDevice(device_);
	fade_step_ = seconds > 0.
		? static_cast<float>(1. / (seconds * kRate)) : 1.f;
	SDL_UnlockAudioDevice(device_);
}

float Audio::noise () {
	seed_ = seed_ * 1664525u + 1013904223u;
	return static_cast<float>(seed_ >> 8) * (1.f / 8388608.f) - 1.f;
}

float Audio::wave (double phase) {
	constexpr size_t kSize = 4096;
	const double turns = phase - std::floor(phase);
	const double at = turns * kSize;
	const size_t index = static_cast<size_t>(at);
	const float frac = static_cast<float>(at - index);
	const float* table = sine_table();
	return table[index] * (1.f - frac) + table[index + 1] * frac;
}

void Audio::strike (double freq, float amp, float decay, float pan, bool bell) {
	Struck* pick = nullptr;
	float quietest = 1e9f;
	for (Struck& note : struck_) {
		if (!note.on) {
			pick = &note;
			break;
		}
		if (note.amp[0] < quietest) {
			quietest = note.amp[0];
			pick = &note;
		}
	}
	if (pick == nullptr) {
		return;
	}
	const double* ratios = bell ? kBell : kAnvil;
	pick->on = true;
	pick->pan = pan;
	for (int i = 0; i < 3; ++i) {
		pick->phase[i] = 0.;
		pick->step[i] = freq * ratios[i] / kRate;
		pick->amp[i] = amp / (1.4f + 1.2f * i);
		// Higher partials die first. That is the whole trick.
		pick->decay[i] = std::pow(decay, 1.f + 0.75f * i);
	}
}

void Audio::beat (int index) {
	const int bar = (index / kBeatsPerBar) % kBars;
	const double root = kRoots[bar];
	const int in_bar = index % kBeatsPerBar;

	// The bellows: every beat, always, quietly. The pulse a room keeps even
	// when nothing is happening in it.
	strike(root * 2., 0.16f + 0.10f * play_, 0.99986f, 0.5f, false);

	// The anvil arrives once the Flow gauge has something in it, on the
	// first and third beats, and answers itself off the beat when hot.
	const float forge = std::clamp((heat_ - 0.15f) / 0.5f, 0.f, 1.f);
	if (forge > 0.02f && (in_bar == 0 || in_bar == 2)) {
		strike(root * 4., 0.12f * forge, 0.99978f,
			in_bar == 0 ? 0.36f : 0.64f, false);
	}
	if (forge > 0.6f && in_bar == 3) {
		strike(root * 6., 0.07f * forge, 0.99975f, 0.5f, false);
	}

	// The lead is the last thing to show up, and only over a hot board.
	const float sing = std::clamp((heat_ - 0.45f) / 0.4f, 0.f, 1.f);
	const int note = kLead[index % kBeats];
	if (sing > 0.02f && note >= 0) {
		strike(root * 2. * std::pow(2., note / 12.), 0.10f * sing, 0.99991f,
			(index % 8) < 4 ? 0.3f : 0.7f, true);
	}
}

void Audio::render_room (float* out, int samples) {
	const int frames = samples / kChannels;
	// The bed follows music volume: it is scenery, not a cue, and a player
	// who turned the music off asked for a dry game.
	const float bed_level = ambience_ ? music_volume_ : 0.f;
	// No hall means open() never ran, which only happens under a test that
	// mixes without a device; the score needs its delay line, the bed does not.
	const float score_level = (music_mode_ == Music::Forge && music_on_
		&& !delay_.empty()) ? music_volume_ * fade_ : 0.f;
	if (bed_level <= 0.f && score_level <= 0.f) {
		return;
	}
	for (int frame = 0; frame < frames; ++frame) {
		// Smoothing, per sample, so a parameter that jumps between frames
		// still arrives as a swell rather than as a click.
		heat_ += (want_heat_ - heat_) * 0.00004f;
		danger_ += (want_danger_ - danger_) * 0.00004f;
		play_ += (want_play_ - play_) * 0.00002f;
		// Overdrive lights fast and lets go slowly.
		burn_ += (want_burn_ - burn_) * (want_burn_ > burn_ ? 0.00030f : 0.00005f);

		float left = 0.f;
		float right = 0.f;

		// --- the furnace ---------------------------------------------------
		// Noise through two poles, opening as the room heats, with a slow
		// wobble on top so it breathes instead of hissing.
		if (bed_level > 0.f) {
			const float glare = std::min(1.4f, heat_ + 0.6f * burn_);
			const float cutoff = 0.0035f + 0.0075f * glare + 0.004f * danger_;
			const float raw = noise();
			roar_lo_ += (raw - roar_lo_) * cutoff;
			roar_lo2_ += (roar_lo_ - roar_lo2_) * cutoff;
			roar_lfo_ += 0.31 / kRate;
			const float breath = 0.72f + 0.28f * wave(roar_lfo_);
			// Idle rooms are nearly silent; a board in trouble is not.
			const float gain = bed_level * breath
				* (0.055f + 0.10f * play_ + 0.20f * heat_ + 0.13f * danger_
					+ 0.26f * burn_);
			// A little air over the rumble, panned the other way, so the
			// bed has some width to it.
			roar_air_ += (raw - roar_air_) * 0.10f;
			const float air = (raw - roar_air_) * gain * 0.16f;
			left += roar_lo2_ * gain * 3.2f + air;
			right += roar_lo2_ * gain * 3.2f - air;
		}

		// --- the score -----------------------------------------------------
		if (score_level > 0.f) {
			const double tempo = kTempo * music_rate_ * (1. + 0.06 * heat_);
			beat_pos_ += tempo / (60. * kRate);
			const int index = static_cast<int>(beat_pos_);
			if (index != beat_last_) {
				beat_last_ = index;
				beat(index % kBeats);
			}
			const int bar = (index / kBeatsPerBar) % kBars;
			const double root = kRoots[bar];

			// The drone: two saws a hair apart and one an octave up, run
			// through a filter that opens with the heat.
			double sawmix = 0.;
			const double steps[3] = {root / kRate, root * 1.006 / kRate, root * 2. / kRate};
			const float weights[3] = {0.5f, 0.42f, 0.20f};
			for (int i = 0; i < 3; ++i) {
				drone_phase_[i] += steps[i];
				if (drone_phase_[i] >= 1.) {
					drone_phase_[i] -= 1.;
				}
				sawmix += (drone_phase_[i] * 2. - 1.) * weights[i];
			}
			const float open = 0.0020f + 0.0075f * heat_ + 0.010f * burn_;
			drone_lp_ += (static_cast<float>(sawmix) - drone_lp_) * open;
			const float drone = drone_lp_ * score_level
				* (0.16f + 0.10f * play_ + 0.12f * heat_);
			left += drone;
			right += drone;

			// Overdrive puts a sub under the whole thing.
			if (burn_ > 0.001f) {
				sub_phase_ += root * 0.5 / kRate;
				const float swell = wave(sub_phase_) * burn_ * score_level * 0.22f;
				left += swell;
				right += swell;
			}

			// The struck voices, into the hall.
			float dry_l = 0.f;
			float dry_r = 0.f;
			for (Struck& note : struck_) {
				if (!note.on) {
					continue;
				}
				float sum = 0.f;
				bool alive = false;
				for (int i = 0; i < 3; ++i) {
					note.phase[i] += note.step[i];
					note.amp[i] *= note.decay[i];
					sum += wave(note.phase[i]) * note.amp[i];
					alive = alive || note.amp[i] > 0.0004f;
				}
				note.on = alive;
				dry_l += sum * (1.f - note.pan);
				dry_r += sum * note.pan;
			}
			dry_l *= score_level;
			dry_r *= score_level;

			const size_t read_l = (delay_at_ + kDelayFrames - kTapLeft) % kDelayFrames;
			const size_t read_r = (delay_at_ + kDelayFrames - kTapRight) % kDelayFrames;
			const float echo_l = delay_[read_l * kChannels];
			const float echo_r = delay_[read_r * kChannels + 1];
			delay_lp_[0] += (echo_l - delay_lp_[0]) * 0.25f;
			delay_lp_[1] += (echo_r - delay_lp_[1]) * 0.25f;
			delay_[delay_at_ * kChannels] = dry_l + delay_lp_[1] * 0.36f;
			delay_[delay_at_ * kChannels + 1] = dry_r + delay_lp_[0] * 0.36f;
			delay_at_ = (delay_at_ + 1) % kDelayFrames;

			left += dry_l + echo_l * 0.42f;
			right += dry_r + echo_r * 0.42f;
		}

		out[frame * kChannels] += left;
		out[frame * kChannels + 1] += right;
	}
}

void Audio::mix (float* out, int samples) {
	std::fill(out, out + samples, 0.f);
	const int frames = samples / kChannels;
	render_room(out, samples);
	if (music_on_ && music_mode_ == Music::Classic && !music_.empty()) {
		// The track reads from a fractional frame position so Overdrive can
		// run it hot: nearest-neighbour per stereo pair, wrap at the end.
		const size_t total = music_.size() / kChannels;
		for (int i = 0; i < samples; ++i) {
			const size_t frame = static_cast<size_t>(music_pos_) % total;
			out[i] += music_[frame * kChannels
				+ static_cast<size_t>(i % kChannels)]
				* music_volume_ * fade_;
			if (i % kChannels == kChannels - 1) {
				music_pos_ += music_rate_;
				if (music_pos_ >= static_cast<double>(total)) {
					music_pos_ -= static_cast<double>(total);
				}
			}
		}
	}
	// One fade step for the whole buffer - about three milliseconds of
	// granularity on a two and a half second fade, which nobody can hear.
	if (fade_step_ > 0.f && music_on_) {
		fade_ = std::max(0.f, fade_ - fade_step_ * frames);
		if (fade_ <= 0.f) {
			music_on_ = false;
		}
	}
	for (Voice& voice : voices_) {
		if (voice.data == nullptr) {
			continue;
		}
		const std::vector<float>& data = *voice.data;
		for (int i = 0; i < samples && voice.at < data.size(); ++i, ++voice.at) {
			out[i] += data[voice.at] * sfx_volume_;
		}
		if (voice.at >= data.size()) {
			voice.data = nullptr;
			voice.at = 0;
		}
	}
	// The room and a full pool of tails can add up; keep the sum inside the
	// device's range rather than letting it wrap into a tear.
	for (int i = 0; i < samples; ++i) {
		out[i] = std::clamp(out[i], -1.f, 1.f);
	}
}

} // namespace gui
} // namespace forcetris
