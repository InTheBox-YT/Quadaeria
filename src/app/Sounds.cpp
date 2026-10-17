#include "app/Sounds.h"
#include <cmath>

namespace qc {
namespace {
using synth::Params;
using synth::Wave;

std::vector<float> gen(uint32_t sr, Params p) { return synth::generate(sr, p); }

std::vector<float> materialHit(uint32_t sr, Sound m, float dur, float vol) {
    Params p;
    p.wave = Wave::Noise;
    p.duration = dur;
    p.volume = vol;
    p.attack = 0.002f;
    switch (m) {
    case Sound::Stone:
        p.lowpass = 0.35f;
        p.decay = 30;
        break;
    case Sound::Wood: {
        p.lowpass = 0.18f;
        p.decay = 26;
        auto v = gen(sr, p);
        Params t;
        t.wave = Wave::Triangle;
        t.freqStart = 190;
        t.freqEnd = 150;
        t.duration = dur;
        t.decay = 30;
        t.volume = vol * 0.6f;
        synth::mix(v, gen(sr, t));
        return v;
    }
    case Sound::Grass:
        p.lowpass = 0.5f;
        p.decay = 22;
        p.volume *= 0.7f;
        break;
    case Sound::Sand:
        p.lowpass = 0.6f;
        p.decay = 18;
        p.volume *= 0.6f;
        break;
    case Sound::Gravel:
        p.lowpass = 0.3f;
        p.decay = 20;
        break;
    case Sound::Glass: {
        p.lowpass = 0.9f;
        p.decay = 14;
        auto v = gen(sr, p);
        Params t;
        t.wave = Wave::Sine;
        t.freqStart = 2400;
        t.freqEnd = 1900;
        t.duration = dur;
        t.decay = 12;
        t.volume = vol * 0.3f;
        synth::mix(v, gen(sr, t));
        return v;
    }
    case Sound::Wool:
        p.lowpass = 0.12f;
        p.decay = 25;
        break;
    case Sound::Snow:
        p.lowpass = 0.25f;
        p.decay = 20;
        p.volume *= 0.8f;
        break;
    }
    return gen(sr, p);
}
}

void Sounds::init(Audio* audio) {
    audio_ = audio;
    if (!audio || !audio->enabled()) return;
    uint32_t sr = audio->sampleRate();
    for (int m = 0; m < 8; ++m) {
        dig_[m] = audio->createSound(materialHit(sr, Sound(m), 0.16f, 0.5f));
        step_[m] = audio->createSound(materialHit(sr, Sound(m), 0.09f, 0.22f));
        place_[m] = audio->createSound(materialHit(sr, Sound(m), 0.12f, 0.45f));
    }
    auto add = [&](Snd s, std::vector<float> v) { snd_[int(s)][0] = audio->createSound(std::move(v)); };
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 1.8f;
        p.decay = 2.6f;
        p.lowpass = 0.05f;
        p.volume = 1.0f;
        p.attack = 0.003f;
        auto v = gen(sr, p);
        Params c;
        c.wave = Wave::Noise;
        c.duration = 0.25f;
        c.decay = 18;
        c.lowpass = 0.6f;
        c.volume = 0.7f;
        synth::mix(v, gen(sr, c));
        Params b;
        b.wave = Wave::Sine;
        b.freqStart = 70;
        b.freqEnd = 30;
        b.duration = 1.2f;
        b.decay = 3;
        b.volume = 0.8f;
        synth::mix(v, gen(sr, b));
        add(Snd::Explode, v);
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 1.4f;
        p.decay = 0.4f;
        p.lowpass = 0.7f;
        p.volume = 0.35f;
        p.attack = 0.05f;
        add(Snd::Fuse, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.6f;
        p.decay = 5;
        p.lowpass = 0.25f;
        p.volume = 0.6f;
        auto v = gen(sr, p);
        Params b;
        b.wave = Wave::Sine;
        b.freqStart = 300;
        b.freqEnd = 900;
        b.duration = 0.25f;
        b.decay = 10;
        b.volume = 0.25f;
        synth::mix(v, gen(sr, b));
        add(Snd::Splash, v);
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.7f;
        p.decay = 4;
        p.lowpass = 0.95f;
        p.volume = 0.4f;
        p.attack = 0.02f;
        add(Snd::Hiss, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Sine;
        p.freqStart = 500;
        p.freqEnd = 1100;
        p.duration = 0.08f;
        p.decay = 30;
        p.volume = 0.4f;
        add(Snd::Pop, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.35f;
        p.decay = 10;
        p.lowpass = 0.06f;
        p.volume = 0.9f;
        auto v = gen(sr, p);
        Params b;
        b.wave = Wave::Sine;
        b.freqStart = 90;
        b.freqEnd = 45;
        b.duration = 0.3f;
        b.decay = 10;
        b.volume = 0.7f;
        synth::mix(v, gen(sr, b));
        add(Snd::Thud, v);
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.5f;
        p.decay = 7;
        p.lowpass = 0.3f;
        p.volume = 0.8f;
        auto v = gen(sr, p);
        Params c;
        c.wave = Wave::Triangle;
        c.freqStart = 140;
        c.freqEnd = 70;
        c.duration = 0.5f;
        c.decay = 5;
        c.volume = 0.5f;
        c.vibratoHz = 40;
        c.vibratoDepth = 0.3f;
        synth::mix(v, gen(sr, c));
        add(Snd::Crack, v);
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.5f;
        p.decay = 8;
        p.lowpass = 0.95f;
        p.volume = 0.5f;
        auto v = gen(sr, p);
        for (int i = 0; i < 3; ++i) {
            Params t;
            t.wave = Wave::Sine;
            t.freqStart = 2000.0f + i * 700;
            t.freqEnd = t.freqStart * 0.9f;
            t.duration = 0.4f;
            t.decay = 9;
            t.volume = 0.12f;
            synth::mix(v, gen(sr, t));
        }
        add(Snd::Glass, v);
    }
    {
        Params p;
        p.wave = Wave::Square;
        p.freqStart = 900;
        p.freqEnd = 900;
        p.duration = 0.04f;
        p.decay = 60;
        p.volume = 0.18f;
        p.lowpass = 0.5f;
        add(Snd::Click, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 0.4f;
        p.decay = 6;
        p.lowpass = 0.5f;
        p.volume = 0.25f;
        add(Snd::Burn, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Saw;
        p.freqStart = 95;
        p.freqEnd = 70;
        p.duration = 1.0f;
        p.attack = 0.18f;
        p.decay = 2.2f;
        p.volume = 0.32f;
        p.lowpass = 0.08f;
        p.vibratoHz = 13;
        p.vibratoDepth = 0.4f;
        auto v = gen(sr, p);
        Params n;
        n.wave = Wave::Noise;
        n.duration = 1.0f;
        n.attack = 0.2f;
        n.decay = 3;
        n.lowpass = 0.04f;
        n.volume = 0.3f;
        synth::mix(v, gen(sr, n));
        add(Snd::Creak, v);
    }
    {
        Params p;
        p.wave = Wave::Noise;
        p.duration = 3.2f;
        p.attack = 0.15f;
        p.decay = 1.1f;
        p.lowpass = 0.025f;
        p.volume = 1.0f;
        auto v = gen(sr, p);
        Params b;
        b.wave = Wave::Sine;
        b.freqStart = 48;
        b.freqEnd = 28;
        b.duration = 3.0f;
        b.attack = 0.1f;
        b.decay = 1.2f;
        b.volume = 0.7f;
        synth::mix(v, gen(sr, b));
        Params c;
        c.wave = Wave::Noise;
        c.duration = 0.4f;
        c.decay = 9;
        c.lowpass = 0.3f;
        c.volume = 0.5f;
        synth::mix(v, gen(sr, c));
        add(Snd::Rumble, v);
    }
    {
        Params p;
        p.wave = Wave::Sine;
        p.freqStart = 1700;
        p.freqEnd = 650;
        p.duration = 0.14f;
        p.decay = 28;
        p.volume = 0.22f;
        add(Snd::Drip, gen(sr, p));
    }
    {
        Params p;
        p.wave = Wave::Sine;
        p.freqStart = 55;
        p.freqEnd = 52;
        p.duration = 7.0f;
        p.attack = 2.0f;
        p.decay = 0.2f;
        p.volume = 0.16f;
        p.vibratoHz = 0.3f;
        p.vibratoDepth = 0.02f;
        auto v = gen(sr, p);
        Params q;
        q.wave = Wave::Triangle;
        q.freqStart = 82.5f;
        q.freqEnd = 80;
        q.duration = 7.0f;
        q.attack = 2.5f;
        q.decay = 0.25f;
        q.volume = 0.08f;
        q.lowpass = 0.2f;
        q.vibratoHz = 0.21f;
        q.vibratoDepth = 0.03f;
        synth::mix(v, gen(sr, q));
        Params n;
        n.wave = Wave::Noise;
        n.duration = 7.0f;
        n.attack = 2.0f;
        n.decay = 0.2f;
        n.lowpass = 0.015f;
        n.volume = 0.12f;
        synth::mix(v, gen(sr, n));
        add(Snd::Drone, v);
    }
}

void Sounds::play(const SoundEvent& e, vec3 listener) {
    if (!audio_ || !audio_->enabled()) return;
    if (lengthSq(e.pos - listener) > 64.0f * 64.0f) return;
    SoundId id;
    int m = std::min(int(e.material), 7);
    switch (e.s) {
    case Snd::Dig: id = dig_[m]; break;
    case Snd::Step: id = step_[m]; break;
    case Snd::Place: id = place_[m]; break;
    default: id = snd_[int(e.s)][0]; break;
    }
    audio_->play3D(id, e.pos, e.vol, e.pitch);
}

void Sounds::uiTick() {
    if (audio_ && audio_->enabled()) audio_->play(snd_[int(Snd::Click)][0], 0.22f, 2.1f);
}
void Sounds::uiPress() {
    if (audio_ && audio_->enabled()) audio_->play(snd_[int(Snd::Thud)][0], 0.28f, 2.2f);
}
void Sounds::uiPop(float pitch) {
    if (audio_ && audio_->enabled()) audio_->play(snd_[int(Snd::Pop)][0], 0.35f, pitch);
}

void Sounds::ui() {
    if (audio_ && audio_->enabled()) audio_->play(snd_[int(Snd::Click)][0], 0.6f, 1.0f);
}

}
