#include "platform/Audio.h"
#include "core/Base.h"
#include <miniaudio.h>
#include <cmath>

namespace qc {
namespace {
constexpr int kVoices = 48;
}

struct Audio::Impl {
    ma_engine engine{};
    struct Voice {
        ma_audio_buffer_ref ref{};
        ma_sound sound{};
        bool ready = false;
    };
    Voice voices[kVoices];
    int nextVoice = 0;
    float silence[16] = {};
};

Audio::Audio() = default;
Audio::~Audio() { shutdown(); }

bool Audio::init(bool enable) {
    if (!enable) {
        QC_LOG("audio disabled");
        return false;
    }
    impl_ = std::make_unique<Impl>();
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
        QC_ERROR("audio device unavailable - continuing silently");
        impl_.reset();
        return false;
    }
    sampleRate_ = ma_engine_get_sample_rate(&impl_->engine);
    for (Impl::Voice& v : impl_->voices) {
        if (ma_audio_buffer_ref_init(ma_format_f32, 1, impl_->silence, 16, &v.ref) != MA_SUCCESS) continue;
        v.ref.sampleRate = sampleRate_;
        if (ma_sound_init_from_data_source(&impl_->engine, &v.ref, 0, nullptr, &v.sound) != MA_SUCCESS) {
            ma_audio_buffer_ref_uninit(&v.ref);
            continue;
        }
        ma_sound_set_attenuation_model(&v.sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&v.sound, 3.0f);
        ma_sound_set_max_distance(&v.sound, 80.0f);
        v.ready = true;
    }
    enabled_ = true;
    QC_LOG("audio: %u Hz, %d voices", sampleRate_, kVoices);
    return true;
}

void Audio::shutdown() {
    if (!impl_) return;
    for (Impl::Voice& v : impl_->voices) {
        if (!v.ready) continue;
        ma_sound_uninit(&v.sound);
        ma_audio_buffer_ref_uninit(&v.ref);
        v.ready = false;
    }
    ma_engine_uninit(&impl_->engine);
    impl_.reset();
    enabled_ = false;
}

SoundId Audio::createSound(std::vector<float> pcm) {
    if (pcm.empty()) pcm.push_back(0.0f);
    clips_.push_back(std::move(pcm));
    return SoundId(clips_.size() - 1);
}

void Audio::play(SoundId s, float volume, float pitch) { playImpl(s, nullptr, volume, pitch); }
void Audio::play3D(SoundId s, vec3 p, float volume, float pitch) { playImpl(s, &p, volume, pitch); }

void Audio::playImpl(SoundId s, const vec3* pos, float volume, float pitch) {
    if (!enabled_ || s >= clips_.size()) return;
    Impl::Voice* voice = nullptr;
    for (int i = 0; i < kVoices; ++i) {
        Impl::Voice& v = impl_->voices[(impl_->nextVoice + i) % kVoices];
        if (v.ready && !ma_sound_is_playing(&v.sound)) {
            voice = &v;
            impl_->nextVoice = (impl_->nextVoice + i + 1) % kVoices;
            break;
        }
    }
    if (!voice) return;
    const std::vector<float>& clip = clips_[s];
    ma_audio_buffer_ref_set_data(&voice->ref, clip.data(), clip.size());
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    ma_sound_set_volume(&voice->sound, volume);
    ma_sound_set_pitch(&voice->sound, pitch);
    if (pos) {
        ma_sound_set_spatialization_enabled(&voice->sound, MA_TRUE);
        ma_sound_set_position(&voice->sound, pos->x, pos->y, pos->z);
    } else {
        ma_sound_set_spatialization_enabled(&voice->sound, MA_FALSE);
    }
    ma_sound_start(&voice->sound);
}

void Audio::setListener(vec3 p, vec3 f) {
    if (!enabled_) return;
    ma_engine_listener_set_position(&impl_->engine, 0, p.x, p.y, p.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, f.x, f.y, f.z);
}

void Audio::setMasterVolume(float v) {
    if (enabled_) ma_engine_set_volume(&impl_->engine, v);
}

namespace synth {
std::vector<float> generate(uint32_t sr, const Params& p) {
    const uint32_t n = uint32_t(p.duration * sr);
    std::vector<float> out(n);
    double phase = 0;
    float lp = 0;
    uint32_t noise = 0x12345678u;
    const float ratio = p.freqEnd / p.freqStart;
    for (uint32_t i = 0; i < n; ++i) {
        float t = float(i) / sr;
        float u = t / p.duration;
        float f = p.freqStart * std::pow(ratio, u);
        if (p.vibratoHz > 0) f *= 1.0f + p.vibratoDepth * std::sin(2 * PI * p.vibratoHz * t);
        phase += f / sr;
        phase -= std::floor(phase);
        float ph = float(phase);
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        float nz = float(noise) / 2147483648.0f - 1.0f;
        float s;
        switch (p.wave) {
        case Wave::Sine: s = std::sin(2 * PI * ph); break;
        case Wave::Square: s = ph < 0.5f ? 0.7f : -0.7f; break;
        case Wave::Saw: s = 2.0f * ph - 1.0f; break;
        case Wave::Triangle: s = 4.0f * std::fabs(ph - 0.5f) - 1.0f; break;
        default: s = nz; break;
        }
        s = s * (1.0f - p.noiseMix) + nz * p.noiseMix;
        lp += (s - lp) * p.lowpass;
        float env = std::min(t / std::max(p.attack, 1e-4f), 1.0f) * std::exp(-t * p.decay);
        env *= std::min(1.0f, (p.duration - t) * 200.0f);
        out[i] = lp * env * p.volume;
    }
    return out;
}

void mix(std::vector<float>& dst, const std::vector<float>& src, float gain) {
    if (dst.size() < src.size()) dst.resize(src.size(), 0.0f);
    for (size_t i = 0; i < src.size(); ++i) dst[i] += src[i] * gain;
}

}
}
