#pragma once
#include "map.h"
#include <utility>
#include <vector>

struct Level {
    Map map;
    std::vector<Unit> units;                 // everyone but the player's army
    std::vector<std::pair<int, int>> starts;  // deployment tiles for the player's army, in roster order
    std::string name, objective;
};

constexpr int kChapterCount = 2;

std::vector<Unit> startingRoster();  // the player's army at the start of the campaign
Level makeLevel(int chapter);
