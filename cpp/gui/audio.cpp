#include "audio.hpp"

#include <algorithm>

#include "stb_vorbis.c"

namespace forcetris {
namespace gui {

namespace {

// The device format everything is converted into once, at load time.
constexpr int kRate = 44100;
constexpr int kChannels = 2;

// The cue names, matching engine/environment.py's SFX_NAMES.
std::vector<std::string> cue_names () {
	std::vector<std::string> names = {
		"move", "rotate", "hold", "lock", "drop", "forced", "clear", "tetris",
		"tspin", "perfect", "b2b", "finesse", "gameover",
		"fusewarn", "flash", "overdrive", "overdrive_end", "burn", "pressure", "hit",
	};
	for (int step = 1; step <= 10; ++step) {
		names.push_back("combo" + std::to_string(step));
	}
	return names;
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

void Audio::open (const std::string& root) {
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
	if (device_ == 0) {
		return;
	}

	// The effects: mono 16-bit WAVs, converted to the device format. A file
	// that is missing or unreadable is simply left out.
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

	// The music, decoded whole. It is half a minute of chiptune, not an album.
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

	SDL_PauseAudioDevice(device_, 0);
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
	for (Voice& voice : voices_) {
		if (voice.data == nullptr) {
			voice.data = &found->second;
			voice.at = 0;
			break;
		}
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
	if (device_ == 0 || music_.empty()) {
		return;
	}
	SDL_LockAudioDevice(device_);
	music_at_ = 0;
	music_pos_ = 0.;
	music_on_ = true;
	fade_ = 1.f;
	fade_step_ = 0.f;
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

void Audio::mix (float* out, int samples) {
	std::fill(out, out + samples, 0.f);
	if (music_on_ && !music_.empty()) {
		// The track reads from a fractional frame position so Overdrive can
		// run it hot: nearest-neighbour per stereo pair, wrap at the end.
		const size_t frames = music_.size() / kChannels;
		for (int i = 0; i < samples; ++i) {
			const size_t frame = static_cast<size_t>(music_pos_) % frames;
			out[i] += music_[frame * kChannels
				+ static_cast<size_t>(i % kChannels)]
				* music_volume_ * fade_;
			if (i % kChannels == kChannels - 1) {
				music_pos_ += music_rate_;
				if (music_pos_ >= static_cast<double>(frames)) {
					music_pos_ -= static_cast<double>(frames);
				}
				// The fade steps per sample pair; silence stops the track.
				if (fade_step_ > 0.f) {
					fade_ = std::max(0.f, fade_ - fade_step_);
					if (fade_ <= 0.f) {
						music_on_ = false;
						break;
					}
				}
			}
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
}

} // namespace gui
} // namespace forcetris
