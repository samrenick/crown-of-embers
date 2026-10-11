#pragma once
#include "raylib.h"

enum class Sfx {
    Cursor, Select, Cancel, Hit, Arrow, Fire, Miss, Crit, Death, Heal, LevelUp, Drop, PhasePlayer, PhaseEnemy,
    Count
};

// Music and sound effects. The map theme has two arrangements of the same score that play in
// lockstep: a calm one for the map and a full battle one that fades in whenever combat starts.
// Everything is silent if the audio device isn't open (autoplay, screenshots) or files are missing.
class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    void update(float dt);
    void play(Sfx s);
    void startTheme();          // restart the map theme from the top
    void setBattle(bool on);    // crossfade toward the battle arrangement (or back)
    void playVictory();
    void playDefeat();
    void toggleMute();
    bool muted() const { return mute; }

private:
    bool ready = false;
    bool mute = false;
    Music rain{}, blaze{};
    bool haveTheme = false;
    Sound sfx[static_cast<int>(Sfx::Count)]{};
    bool haveSfx[static_cast<int>(Sfx::Count)]{};
    Sound victory{}, defeat{};
    bool haveVictory = false, haveDefeat = false;

    float themeVol = 0;   // fades the theme in/out as a whole
    float themeTarget = 1;
    float blazeMix = 0;   // 0 = calm arrangement, 1 = battle arrangement
    bool battle = false;
    float sinceBattle = 0;  // seconds since combat ended; the battle music lingers a moment
};
