#pragma once
#include "map.h"
#include <vector>

struct Level {
    Map map;
    std::vector<Unit> units;
    std::string objective;
};

Level makeLevel();
