#pragma once
#include "raylib.h"
#include "types.h"
#include <string>
#include <vector>

enum class Terrain { Plain, Forest, Mountain, Water, Wall, Fort, Throne, Road, Rubble };

struct TerrainInfo {
    const char* name;
    int def, avoid;
    int heal;  // percent of max HP restored at the start of the owner's phase
    Color color;
};

const TerrainInfo& terrainInfo(Terrain t);
int moveCost(Terrain t, MoveType m);  // -1 if impassable

struct Map {
    int w = 0, h = 0;
    std::vector<Terrain> tiles;

    // '.' plain, 'f' forest, 'm' mountain, '~' water, '#' wall, 'F' fort, 'T' throne,
    // '=' road, 'r' rubble
    static Map fromStrings(const std::vector<std::string>& rows);
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    Terrain at(int x, int y) const { return tiles[y * w + x]; }
};
