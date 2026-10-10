#pragma once
#include <string>

enum class Team { Player, Enemy };
enum class MoveType { Infantry, Armored, Cavalry, Flier };
enum class WeaponType { Sword, Lance, Axe, Bow, Tome, Staff };
enum class AIMode {
    Charge,   // always advances toward the nearest foe
    InRange,  // attacks anything it can reach this turn, otherwise waits
    Hold,     // never moves; attacks only from where it stands
};

inline unsigned bit(MoveType m) { return 1u << static_cast<unsigned>(m); }

struct Weapon {
    std::string name;
    WeaponType type;
    int might, hit, crit, weight, minRange, maxRange;
    unsigned effective = 0;  // bitmask of MoveTypes this weapon deals triple might against
};

struct Stats {
    int hp = 0, str = 0, mag = 0, skl = 0, spd = 0, lck = 0, def = 0, res = 0;
};

constexpr int kStatCount = 8;
constexpr int Stats::*kStatMembers[kStatCount] = {
    &Stats::hp, &Stats::str, &Stats::mag, &Stats::skl,
    &Stats::spd, &Stats::lck, &Stats::def, &Stats::res,
};
constexpr const char* kStatNames[kStatCount] = {"HP", "Str", "Mag", "Skl", "Spd", "Lck", "Def", "Res"};

struct Unit {
    std::string name, className;
    Team team = Team::Player;
    MoveType moveType = MoveType::Infantry;
    int level = 1, exp = 0;
    Stats stats;    // stats.hp is max HP
    Stats growths;  // percent chance for each stat to rise on level up
    int hp = 0, mov = 5;
    Weapon weapon;
    int x = 0, y = 0;
    bool done = false, alive = true;
    bool isLord = false, isBoss = false;
    AIMode ai = AIMode::Charge;
    float drawX = 0, drawY = 0;  // render position in tiles (differs from x/y while walking)
};
