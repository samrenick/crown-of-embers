#include "audio.h"
#include "assets.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr float kMusicVolume = 0.75f;
constexpr float kBattleFadeIn = 0.5f;   // seconds to swell into the battle arrangement
constexpr float kBattleLinger = 1.5f;   // seconds the battle arrangement holds after combat
constexpr float kBattleFadeOut = 2.0f;  // seconds to settle back to the calm arrangement

const char* const kSfxFiles[] = {
    "cursor.wav", "select.wav", "cancel.wav", "hit.wav", "arrow.wav", "fire.wav", "miss.wav",
    "crit.qoa", "death.wav", "heal.qoa", "levelup.qoa", "drop.wav", "phase_player.qoa", "phase_enemy.qoa",
};
static_assert(sizeof(kSfxFiles) / sizeof(kSfxFiles[0]) == static_cast<int>(Sfx::Count));

const float kSfxVolume[] = {
    0.5f, 0.6f, 0.6f, 0.8f, 0.8f, 0.8f, 0.6f, 0.7f, 0.7f, 0.8f, 0.8f, 0.7f, 0.55f, 0.6f,
};

bool loadSound(const char* file, Sound& out) {
    std::string path = findAsset(std::string("audio/") + file);
    if (path.empty()) return false;
    out = LoadSound(path.c_str());
    return IsSoundValid(out);
}

bool loadMusic(const char* file, Music& out) {
    std::string path = findAsset(std::string("audio/") + file);
    if (path.empty()) return false;
    out = LoadMusicStream(path.c_str());
    return IsMusicValid(out);
}

float approach(float v, float target, float step) {
    return v < target ? std::min(target, v + step) : std::max(target, v - step);
}

}  // namespace

Audio::Audio() {
    ready = IsAudioDeviceReady();
    if (!ready) return;
    bool a = loadMusic("theme_rain.qoa", rain), b = loadMusic("theme_blaze.qoa", blaze);
    haveTheme = a && b;
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
        haveSfx[i] = loadSound(kSfxFiles[i], sfx[i]);
        if (haveSfx[i]) SetSoundVolume(sfx[i], kSfxVolume[i]);
    }
    haveVictory = loadSound("victory.qoa", victory);
    haveDefeat = loadSound("defeat.qoa", defeat);
}

Audio::~Audio() {
    if (!ready) return;
    if (haveTheme) {
        UnloadMusicStream(rain);
        UnloadMusicStream(blaze);
    }
    for (int i = 0; i < static_cast<int>(Sfx::Count); ++i)
        if (haveSfx[i]) UnloadSound(sfx[i]);
    if (haveVictory) UnloadSound(victory);
    if (haveDefeat) UnloadSound(defeat);
}

void Audio::update(float dt) {
    if (!ready || !haveTheme) return;
    if (battle) {
        sinceBattle = 0;
        blazeMix = approach(blazeMix, 1, dt / kBattleFadeIn);
    } else {
        sinceBattle += dt;
        if (sinceBattle > kBattleLinger) blazeMix = approach(blazeMix, 0, dt / kBattleFadeOut);
    }
    themeVol = approach(themeVol, themeTarget, dt / (themeTarget > themeVol ? 1.5f : 0.6f));
    // Equal-power crossfade so the overall loudness holds steady through the blend.
    float v = themeVol * kMusicVolume;
    SetMusicVolume(rain, v * std::cos(blazeMix * 1.5707963f));
    SetMusicVolume(blaze, v * std::sin(blazeMix * 1.5707963f));
    UpdateMusicStream(rain);
    UpdateMusicStream(blaze);
}

void Audio::play(Sfx s) {
    int i = static_cast<int>(s);
    if (ready && haveSfx[i]) PlaySound(sfx[i]);
}

void Audio::startTheme() {
    if (!ready || !haveTheme) return;
    if (haveVictory) StopSound(victory);
    if (haveDefeat) StopSound(defeat);
    // Both arrangements start together and stream at the same rate, so they stay in lockstep.
    StopMusicStream(rain);
    StopMusicStream(blaze);
    PlayMusicStream(rain);
    PlayMusicStream(blaze);
    themeVol = 0;
    themeTarget = 1;
    blazeMix = 0;
    battle = false;
    update(0);
}

void Audio::setBattle(bool on) { battle = on; }

void Audio::playVictory() {
    themeTarget = 0;
    if (ready && haveVictory) PlaySound(victory);
}

void Audio::playDefeat() {
    themeTarget = 0;
    if (ready && haveDefeat) PlaySound(defeat);
}

void Audio::toggleMute() {
    mute = !mute;
    if (ready) SetMasterVolume(mute ? 0.0f : 1.0f);
}
