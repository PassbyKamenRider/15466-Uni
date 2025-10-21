#include "load_wav.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <cassert>
#include <algorithm>

constexpr uint32_t AUDIO_RATE = 48000;

void load_wav(std::string const &filename, std::vector< float > *data_) {
	assert(data_);
	auto &data = *data_;

	SDL_AudioSpec audio_spec;
	Uint8 *audio_buf = nullptr;
	Uint32 audio_len = 0;

	if (!SDL_LoadWAV(filename.c_str(), &audio_spec, &audio_buf, &audio_len)) {
		throw std::runtime_error("Failed to load WAV file '" + filename + "'; SDL says \"" + std::string(SDL_GetError()) + "\"");
	}
	SDL_AudioSpec out_spec{ .format=SDL_AUDIO_F32, .channels=1, .freq=AUDIO_RATE };
	if (audio_spec.format != out_spec.format || audio_spec.channels != out_spec.channels || audio_spec.freq != out_spec.freq) {
		Uint8 *out_buf = NULL;
		int out_len = 0;
		std::cout << "WAV file '" + filename + "' didn't load as " + std::to_string(AUDIO_RATE) + " Hz, float32, mono; converting." << std::endl;

		if (!SDL_ConvertAudioSamples(&audio_spec, audio_buf, audio_len, &out_spec, &out_buf, &out_len)) {
			//shouldn't happen, but if it does treat as fatal
			std::cout << "  Audio conversion failed: " << SDL_GetError() << std::endl;
			exit(1);
		}

		//discard loaded audio:
		SDL_free(audio_buf);

		//replace with converted audio:
		audio_buf = out_buf;
		audio_len = out_len;
	}

	data.assign(reinterpret_cast< float * >(audio_buf), reinterpret_cast< float * >(audio_buf + audio_len));

	SDL_free(audio_buf);
	audio_buf = NULL;

	/* DEBUG: give audio range info:
	float min = 0.0f;
	float max = 0.0f;
	for (auto d : data) {
		min = std::min(min, d);
		max = std::max(max, d);
	}
	std::cout << "Range of " << filename << ": " << min << ", " << max << std::endl;
	*/
}
