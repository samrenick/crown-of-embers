#include "map.h"

static const TerrainInfo kTerrain[] = {
    {"Plain",    0, 0,  0,  {118, 170, 82, 255}},
    {"Forest",   1, 20, 0,  {78, 138, 66, 255}},
    {"Mountain", 2, 30, 0,  {158, 132, 96, 255}},
    {"Water",    0, 0,  0,  {64, 116, 186, 255}},
    {"Wall",     0, 0,  0,  {96, 96, 108, 255}},
    {"Fort",     2, 20, 20, {176, 152, 112, 255}},
    {"Throne",   3, 30, 20, {150, 116, 160, 255}},
    {"Road",     0, 0,  0,  {150, 142, 126, 255}},
    {"Rubble",   1, 10, 0,  {128, 118, 104, 255}},
    {"Peak",     3, 40, 0,  {124, 116, 110, 255}},
};

// Columns: Infantry, Armored, Cavalry, Flier
static const int kMoveCost[][4] = {
    {1, 1, 1, 1},     // Plain
    {2, 2, 3, 1},     // Forest
    {3, -1, -1, 1},   // Mountain
    {-1, -1, -1, 1},  // Water
    {-1, -1, -1, -1}, // Wall
    {2, 2, 2, 1},     // Fort
    {1, 1, 1, 1},     // Throne
    {1, 1, 1, 1},     // Road
    {2, 2, 3, 1},     // Rubble
    {-1, -1, -1, 2},  // Peak: only fliers can cross
};

const TerrainInfo& terrainInfo(Terrain t) { return kTerrain[static_cast<int>(t)]; }

int moveCost(Terrain t, MoveType m) {
    return kMoveCost[static_cast<int>(t)][static_cast<int>(m)];
}

Map Map::fromStrings(const std::vector<std::string>& rows) {
    Map m;
    m.h = static_cast<int>(rows.size());
    m.w = m.h ? static_cast<int>(rows[0].size()) : 0;
    for (const auto& row : rows) {
        for (char c : row) {
            switch (c) {
                case 'f': m.tiles.push_back(Terrain::Forest); break;
                case 'm': m.tiles.push_back(Terrain::Mountain); break;
                case '~': m.tiles.push_back(Terrain::Water); break;
                case '#': m.tiles.push_back(Terrain::Wall); break;
                case 'F': m.tiles.push_back(Terrain::Fort); break;
                case 'T': m.tiles.push_back(Terrain::Throne); break;
                case '=': m.tiles.push_back(Terrain::Road); break;
                case 'r': m.tiles.push_back(Terrain::Rubble); break;
                case '^': m.tiles.push_back(Terrain::Peak); break;
                default:  m.tiles.push_back(Terrain::Plain); break;
            }
        }
    }
    return m;
}
