#pragma once
#include "core/Math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace qc {
using SoundId = uint16_t;
constexpr SoundId NoSound = 0xFFFF;

class Audio {
public:
    Audio();
    ~Audio();
    bool init(bool enable = true);
    void shutdown();
    bool enabled() const { return enabled_; }
    uint32_t sampleRate() const { return sampleRate_; }

    SoundId createSound(std::vector<float> pcm);

    void play(SoundId s, float volume = 1.0f, float pitch = 1.0f);
    void play3D(SoundId s, vec3 position, float volume = 1.0f, float pitch = 1.0f);
    void setListener(vec3 position, vec3 forward);
    void setMasterVolume(float v);

private:
    void playImpl(SoundId s, const vec3* pos, float volume, float pitch);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool enabled_ = false;
    uint32_t sampleRate_ = 48000;
    std::vector<std::vector<float>> clips_;
};

namespace synth {
enum class Wave { Sine, Square, Saw, Triangle, Noise };
struct Params {
    Wave wave = Wave::Sine;
    float duration = 0.2f;
    float freqStart = 440.0f;
    float freqEnd = 440.0f;
    float attack = 0.005f;
    float decay = 8.0f;
    float volume = 0.5f;
    float noiseMix = 0.0f;
    float lowpass = 1.0f;
    float vibratoHz = 0.0f, vibratoDepth = 0.0f;
};
std::vector<float> generate(uint32_t sampleRate, const Params& p);
void mix(std::vector<float>& dst, const std::vector<float>& src, float gain = 1.0f);
}

}
