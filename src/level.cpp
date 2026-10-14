#include "level.h"

namespace {

using WT = WeaponType;
using MT = MoveType;

const Weapon kRapier     {"Rapier",       WT::Sword, 7,  95, 10, 5,  1, 1, bit(MT::Armored) | bit(MT::Cavalry)};
const Weapon kIronSword  {"Iron Sword",   WT::Sword, 5,  90, 0,  5,  1, 1};
const Weapon kSteelSword {"Steel Sword",  WT::Sword, 8,  75, 0,  10, 1, 1};
const Weapon kIronLance  {"Iron Lance",   WT::Lance, 7,  80, 0,  8,  1, 1};
const Weapon kJavelin    {"Javelin",      WT::Lance, 6,  65, 0,  11, 1, 2};
const Weapon kIronAxe    {"Iron Axe",     WT::Axe,   8,  75, 0,  10, 1, 1};
const Weapon kHandAxe    {"Hand Axe",     WT::Axe,   7,  60, 0,  12, 1, 2};
const Weapon kIronBow    {"Iron Bow",     WT::Bow,   6,  85, 0,  5,  2, 2, bit(MT::Flier)};
const Weapon kFire       {"Fire",         WT::Tome,  5,  90, 0,  4,  1, 2};
const Weapon kTwinDaggers{"Twin Daggers", WT::Sword, 4,  95, 15, 2,  1, 1};
const Weapon kHeal       {"Heal",         WT::Staff, 0,  0,  0,  2,  1, 1};

Unit make(const char* name, const char* cls, Team team, MT mt, int level, int mov,
          Stats stats, Stats growths, const Weapon& w, int x, int y, AIMode ai = AIMode::Charge) {
    Unit u;
    u.name = name;
    u.className = cls;
    u.team = team;
    u.moveType = mt;
    u.level = level;
    u.mov = mov;
    u.stats = stats;
    u.growths = growths;
    u.hp = stats.hp;
    u.weapon = w;
    u.x = x;
    u.y = y;
    u.ai = ai;
    return u;
}

}  // namespace

std::vector<Unit> startingRoster() {
    const Team P = Team::Player;
    std::vector<Unit> u;
    //                                                     HP Str Mag Skl Spd Lck Def Res
    u.push_back(make("Rowan", "Lord", P, MT::Infantry, 1, 5, {20, 6, 0, 7, 8, 6, 5, 1},
                     {80, 45, 10, 50, 55, 45, 30, 25}, kRapier, 0, 0));
    u.back().isLord = true;
    u.push_back(make("Mira", "Cavalier", P, MT::Cavalry, 2, 7, {22, 7, 0, 6, 7, 4, 7, 1},
                     {85, 45, 5, 40, 40, 30, 25, 15}, kIronLance, 0, 0));
    u.push_back(make("Garrick", "Knight", P, MT::Armored, 3, 4, {26, 9, 0, 5, 2, 2, 12, 1},
                     {90, 50, 0, 30, 15, 25, 50, 15}, kIronLance, 0, 0));
    u.push_back(make("Tobin", "Archer", P, MT::Infantry, 2, 5, {18, 6, 0, 8, 7, 4, 3, 2},
                     {70, 40, 5, 55, 45, 35, 15, 15}, kIronBow, 0, 0));
    u.push_back(make("Elsa", "Mage", P, MT::Infantry, 2, 5, {16, 0, 7, 6, 7, 5, 2, 6},
                     {55, 5, 55, 40, 50, 35, 10, 40}, kFire, 0, 0));
    u.push_back(make("Pell", "Pegasus Knight", P, MT::Flier, 2, 7, {21, 7, 0, 9, 12, 7, 5, 7},
                     {70, 45, 10, 50, 65, 50, 20, 40}, kIronLance, 0, 0));
    u.push_back(make("Nia", "Cleric", P, MT::Infantry, 1, 5, {15, 0, 5, 4, 6, 8, 1, 7},
                     {50, 5, 50, 30, 45, 60, 10, 50}, kHeal, 0, 0));
    u.push_back(make("Sable", "Thief", P, MT::Infantry, 2, 6, {17, 5, 0, 10, 12, 6, 3, 2},
                     {65, 35, 5, 55, 65, 50, 15, 25}, kTwinDaggers, 0, 0));
    return u;
}

namespace {

Level borderFields() {
    Level lv;
    lv.name = "Chapter 1: The Border Fields";
    lv.map = Map::fromStrings({
        "mm....ff...#####",
        "m...f......#.T.#",
        "...fff..~~.#...#",
        "..F.f...~~.##.##",
        "........~~......",
        "..ff....~~..ff..",
        "......f.......F.",
        "..mm...ff..~~...",
        "...m.......~~.f.",
        ".....F..ff......",
        "..............mm",
    });
    lv.objective = "Defeat the boss";
    lv.starts = {{2, 9}, {1, 8}, {3, 10}, {1, 10}, {4, 9}, {0, 9}, {2, 10}, {0, 10}};

    const Team E = Team::Enemy;
    const Stats none{};
    auto& u = lv.units;
    //                                                     HP Str Mag Skl Spd Lck Def Res
    u.push_back(make("Vargus", "General", E, MT::Armored, 8, 4, {27, 10, 0, 7, 3, 3, 9, 3},
                     none, kJavelin, 13, 1, AIMode::Hold));
    u.back().isBoss = true;
    u.push_back(make("Soldier", "Soldier", E, MT::Infantry, 2, 5, {20, 6, 0, 4, 4, 1, 5, 1},
                     none, kIronLance, 13, 3, AIMode::InRange));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 2, 5, {18, 5, 0, 5, 5, 1, 3, 1},
                     none, kIronBow, 12, 2, AIMode::InRange));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 2, 5, {22, 7, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 5, 2));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 2, 5, {22, 7, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 4, 1));
    u.push_back(make("Mercenary", "Mercenary", E, MT::Infantry, 3, 5, {22, 7, 0, 8, 8, 2, 4, 1},
                     none, kSteelSword, 10, 5));
    u.push_back(make("Soldier", "Soldier", E, MT::Infantry, 2, 5, {20, 6, 0, 4, 4, 1, 5, 1},
                     none, kIronLance, 14, 6, AIMode::InRange));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 2, 5, {18, 5, 0, 5, 5, 1, 3, 1},
                     none, kIronBow, 10, 4));
    u.push_back(make("Mage", "Mage", E, MT::Infantry, 3, 5, {18, 0, 6, 5, 5, 1, 2, 5},
                     none, kFire, 12, 5, AIMode::InRange));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 2, 5, {22, 7, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 14, 8));
    return lv;
}

// An abandoned city split by a canal. Streets are open ground, rubble slows everyone but fliers,
// and the boss holds the throne of a ruined cathedral at the north end.
Level ashenCity() {
    Level lv;
    lv.name = "Chapter 2: The Ashen City";
    lv.map = Map::fromStrings({
        "#r==###T###==r.#",
        "#.=.#.....#.=..#",
        "..=.##r..##.=r..",
        "==============r=",
        "~~=~~~=~~~~~=~~~",
        "r.=.##.=.#F.=.r.",
        "..=.##r=..#.=...",
        "===========r====",
        ".#.r##.=..##.=#.",
        ".F..#..=.r..r=..",
        "....r..=....#...",
    });
    lv.objective = "Defeat the boss";
    lv.starts = {{2, 9}, {0, 10}, {3, 10}, {1, 10}, {0, 9}, {2, 10}, {3, 9}, {0, 8}};

    const Team E = Team::Enemy;
    const Stats none{};
    auto& u = lv.units;
    //                                                     HP Str Mag Skl Spd Lck Def Res
    u.push_back(make("Corvin", "Mercenary", E, MT::Infantry, 10, 5, {28, 9, 0, 9, 8, 4, 4, 1},
                     none, kSteelSword, 7, 0, AIMode::Hold));
    u.back().isBoss = true;
    u.push_back(make("Knight", "Knight", E, MT::Armored, 4, 4, {25, 8, 0, 4, 2, 1, 10, 0},
                     none, kIronLance, 8, 2, AIMode::Hold));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 3, 5, {19, 6, 0, 6, 5, 1, 3, 1},
                     none, kIronBow, 5, 1, AIMode::InRange));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 3, 5, {19, 6, 0, 6, 5, 1, 3, 1},
                     none, kIronBow, 9, 1, AIMode::InRange));
    u.push_back(make("Mage", "Mage", E, MT::Infantry, 3, 5, {18, 0, 6, 5, 5, 1, 2, 5},
                     none, kFire, 13, 2, AIMode::InRange));
    u.push_back(make("Soldier", "Soldier", E, MT::Infantry, 3, 5, {21, 7, 0, 5, 4, 1, 6, 1},
                     none, kIronLance, 6, 4, AIMode::InRange));
    u.push_back(make("Soldier", "Soldier", E, MT::Infantry, 3, 5, {21, 7, 0, 5, 4, 1, 6, 1},
                     none, kIronLance, 12, 4, AIMode::InRange));
    u.push_back(make("Mercenary", "Mercenary", E, MT::Infantry, 4, 5, {23, 7, 0, 8, 8, 2, 4, 1},
                     none, kSteelSword, 10, 5));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 3, 5, {23, 8, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 6, 6));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 3, 5, {23, 8, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 14, 5));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 2, 5, {22, 7, 0, 3, 5, 0, 3, 0},
                     none, kIronAxe, 9, 9));
    return lv;
}

// A mountain whose summit only fliers can cross. Roads wind around both flanks to a hill keep on
// the far side; armored and mounted units must take the long way, while infantry can scramble over
// the shoulders. Enemy pegasus knights strike straight across the peaks.
Level emberPeak() {
    Level lv;
    lv.name = "Chapter 3: Ember Peak";
    lv.map = Map::fromStrings({
        "mm.f.#..T..#.fmm",
        "m.===.......===m",
        "..=f.##F.F##f.=.",
        ".f=.mm^^^^^^m.=f",
        "..=mm^^^^^^^m.=.",
        "f.=m^^^^^^^^mm=.",
        "..=.m^^^^^^mm.=.",
        ".f=..mmm^mmm.f=.",
        "..==..f.m.f..==.",
        "...===========..",
        "F.f............F",
    });
    lv.objective = "Defeat the boss";
    lv.starts = {{8, 10}, {9, 10}, {6, 9}, {7, 9}, {8, 9}, {9, 9}, {7, 10}, {6, 10}};

    const Team E = Team::Enemy;
    const Stats none{};
    auto& u = lv.units;
    //                                                     HP Str Mag Skl Spd Lck Def Res
    u.push_back(make("Skarde", "Brigand", E, MT::Infantry, 12, 5, {34, 11, 0, 8, 9, 3, 6, 1},
                     none, kHandAxe, 8, 0, AIMode::Hold));
    u.back().isBoss = true;
    u.push_back(make("Knight", "Knight", E, MT::Armored, 5, 4, {26, 9, 0, 5, 2, 1, 11, 1},
                     none, kIronLance, 5, 1, AIMode::Hold));
    u.push_back(make("Knight", "Knight", E, MT::Armored, 5, 4, {26, 9, 0, 5, 2, 1, 11, 1},
                     none, kIronLance, 11, 1, AIMode::Hold));
    u.push_back(make("Mage", "Mage", E, MT::Infantry, 4, 5, {19, 0, 7, 6, 6, 1, 2, 6},
                     none, kFire, 8, 1, AIMode::InRange));
    u.push_back(make("Soldier", "Soldier", E, MT::Infantry, 4, 5, {22, 7, 0, 5, 5, 1, 6, 1},
                     none, kIronLance, 7, 2, AIMode::InRange));
    u.push_back(make("Mercenary", "Mercenary", E, MT::Infantry, 5, 5, {24, 8, 0, 9, 9, 2, 5, 1},
                     none, kSteelSword, 13, 2));
    u.push_back(make("Pegasus Knight", "Pegasus Knight", E, MT::Flier, 4, 7, {19, 6, 0, 7, 10, 3, 4, 6},
                     none, kIronLance, 7, 4, AIMode::InRange));
    u.push_back(make("Pegasus Knight", "Pegasus Knight", E, MT::Flier, 4, 7, {19, 6, 0, 7, 10, 3, 4, 6},
                     none, kIronLance, 10, 5, AIMode::InRange));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 4, 5, {20, 6, 0, 7, 6, 1, 3, 1},
                     none, kIronBow, 4, 4, AIMode::InRange));
    u.push_back(make("Archer", "Archer", E, MT::Infantry, 4, 5, {20, 6, 0, 7, 6, 1, 3, 1},
                     none, kIronBow, 12, 4, AIMode::InRange));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 4, 5, {25, 8, 0, 4, 6, 0, 3, 0},
                     none, kIronAxe, 3, 5));
    u.push_back(make("Brigand", "Brigand", E, MT::Infantry, 4, 5, {25, 8, 0, 4, 6, 0, 3, 0},
                     none, kIronAxe, 13, 5));
    return lv;
}

}  // namespace

Level makeLevel(int chapter) {
    switch (chapter) {
        case 1: return ashenCity();
        case 2: return emberPeak();
        default: return borderFields();
    }
}
