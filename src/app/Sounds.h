#pragma once
#include "platform/Audio.h"
#include "sim/Sim.h"

namespace qc {
class Sounds {
public:
    void init(Audio* audio);
    void play(const SoundEvent& e, vec3 listener);
    void ui();
    void uiTick();
    void uiPress();
    void uiPop(float pitch = 1.0f);

private:
    Audio* audio_ = nullptr;
    SoundId snd_[int(Snd::Count)][8] = {};
    SoundId dig_[8] = {}, step_[8] = {}, place_[8] = {};
};

}
