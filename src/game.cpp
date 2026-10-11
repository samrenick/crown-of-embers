#include "game.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>
#include <tuple>

namespace {

constexpr int MAP_PX_W = MAP_W * TILE;
constexpr int MAP_PX_H = MAP_H * TILE;
constexpr int MENU_ITEM_H = 30;
constexpr int MENU_W = 130;

constexpr Color kPanelBg{26, 28, 38, 255};
constexpr Color kPanelLine{60, 64, 84, 255};
constexpr Color kText{230, 232, 240, 255};
constexpr Color kDim{150, 156, 176, 255};
constexpr Color kPlayerColor{52, 108, 220, 255};
constexpr Color kEnemyColor{202, 54, 54, 255};
constexpr Color kMoveColor{70, 130, 255, 255};
constexpr Color kAttackColor{240, 60, 60, 255};
constexpr Color kHealColor{60, 210, 120, 255};
constexpr float kStrikeImpact = 0.22f;        // on the map: seconds into a strike when damage lands
constexpr float kSceneImpact = 0.5f;          // battle scene: slower, so the swing and the hit read clearly
constexpr float kSceneStrike = 1.15f;         // battle scene: seconds per strike
constexpr float kSceneIntro = 0.6f;           // battle scene: fade in and stand-off before the first strike
constexpr float kSceneOutro = 1.0f;           // battle scene: hold after the last strike (deaths, HP settling)
constexpr float kSpriteScale = 52.0f / 112.0f;  // sprite art is authored ~112px tall; drawn 52px tall

// Seconds per frame for each animation; idle and walk loop, the rest play once.
float frameTime(const std::string& anim) {
    if (anim == "idle") return 0.18f;
    if (anim == "walk") return 0.12f;
    return 0.1f;
}

std::string findAsset(const std::string& file) {
    // Look next to the working directory first (make run), then beside the binary (build/fe),
    // then inside a macOS app bundle (Contents/MacOS/fe -> Contents/Resources/assets).
    std::string dir = GetApplicationDirectory();
    for (std::string path : {"assets/sprites/" + file, dir + "../assets/sprites/" + file,
                             dir + "../Resources/assets/sprites/" + file})
        if (FileExists(path.c_str())) return path;
    return "";
}

bool pressed(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }
int manhattan(int x0, int y0, int x1, int y1) { return std::abs(x0 - x1) + std::abs(y0 - y1); }
Vector2 tileCenter(float x, float y) { return {x * TILE + TILE / 2.0f, y * TILE + TILE / 2.0f}; }

void drawTextCentered(const char* text, int cx, int y, int size, Color c) {
    DrawText(text, cx - MeasureText(text, size) / 2, y, size, c);
}

void drawHpBar(float x, float y, float w, float h, int hp, int maxHp) {
    float ratio = maxHp > 0 ? static_cast<float>(hp) / maxHp : 0;
    Color fill = ratio > 0.5f ? Color{90, 210, 110, 255} : ratio > 0.25f ? Color{230, 200, 60, 255} : Color{230, 70, 60, 255};
    DrawRectangleRec({x, y, w, h}, {20, 20, 24, 220});
    DrawRectangleRec({x, y, w * ratio, h}, fill);
}

const char* weaponTypeName(WeaponType t) {
    switch (t) {
        case WeaponType::Sword: return "Sword";
        case WeaponType::Lance: return "Lance";
        case WeaponType::Axe:   return "Axe";
        case WeaponType::Bow:   return "Bow";
        case WeaponType::Tome:  return "Tome";
        case WeaponType::Staff: return "Staff";
    }
    return "";
}

}  // namespace

// ---------------------------------------------------------------------------
// Setup and queries
// ---------------------------------------------------------------------------

Game::Game(bool autoplay, int startChapter) : rng(std::random_device{}()), autoplay(autoplay) {
    loadSprites();
    newCampaign(startChapter);
}

Game::~Game() {
    for (auto& [name, sprite] : sprites) {
        UnloadTexture(sprite.tex);
        UnloadTexture(sprite.battleTex);
    }
}

void Game::loadSprites() {
    struct Entry {
        const char* cls;
        const char* name;
        bool facesLeft;
    };
    const Entry files[] = {
        {"Knight", "knight", false},   {"Archer", "archer", false},   {"Cleric", "cleric", false},
        {"Cavalier", "cavalier", false}, {"Mage", "mage", false},     {"Lord", "lord", true},
        {"Pegasus Knight", "pegasus", false}, {"General", "general", false}, {"Brigand", "brigand", false},
        {"Mercenary", "mercenary", false}, {"Soldier", "soldier", false},
    };
    for (const auto& [cls, name, facesLeft] : files) {
        // Prefer an animation atlas (<name>_anim.png + .txt layout); fall back to a single still.
        std::string meta = findAsset(std::string(name) + "_anim.txt");
        std::string png = findAsset(meta.empty() ? std::string(name) + ".png" : std::string(name) + "_anim.png");
        if (png.empty()) continue;
        Sprite sp;
        sp.tex = LoadTexture(png.c_str());
        sp.facesLeft = meta.empty() && facesLeft;  // facesLeft describes the still; atlases are built facing right
        GenTextureMipmaps(&sp.tex);
        SetTextureFilter(sp.tex, TEXTURE_FILTER_TRILINEAR);
        sp.battleTex = LoadTexture(png.c_str());
        SetTextureFilter(sp.battleTex, TEXTURE_FILTER_POINT);
        sp.cellW = sp.tex.width;
        sp.cellH = sp.feetY = sp.tex.height;
        sp.anchorX = sp.tex.width / 2;
        if (!meta.empty()) {
            // Format: "cell <w> <h> <anchorX> <feetY>", optional "scale <battleScale>", then one line per atlas row:
            // "<anim> <frames> [hitFrame] [@col,col,...]". The @ list picks which columns play (hitFrame indexes
            // into it). A leading '-' on the name skips that row.
            std::ifstream in(meta);
            std::string line;
            int row = 0;
            while (std::getline(in, line)) {
                std::istringstream ls(line);
                std::string key;
                if (!(ls >> key) || key[0] == '#') continue;
                if (key == "cell") {
                    ls >> sp.cellW >> sp.cellH >> sp.anchorX >> sp.feetY;
                } else if (key == "scale") {
                    ls >> sp.battleScale;
                } else if (key[0] == '-') {
                    ++row;
                } else {
                    Anim a{row++, 0, -1, {}};
                    std::string tok;
                    ls >> a.frames;
                    while (ls >> tok) {
                        if (tok[0] != '@') {
                            a.hit = std::stoi(tok);
                            continue;
                        }
                        std::istringstream cs(tok.substr(1));
                        for (std::string col; std::getline(cs, col, ',');) a.cols.push_back(std::stoi(col));
                    }
                    if (!a.cols.empty()) a.frames = static_cast<int>(a.cols.size());
                    sp.anims[key] = a;
                }
            }
        }
        sprites[cls] = sp;
    }
}

void Game::newCampaign(int startChapter) {
    roster = startingRoster();
    chapter = std::clamp(startChapter, 0, kChapterCount - 1);
    this->startChapter();
}

void Game::advanceChapter() {
    // Survivors carry over with their levels and EXP, healed and set down.
    std::vector<Unit> next;
    for (const Unit& u : units) {
        if (u.team != Team::Player || !u.alive) continue;
        Unit s = u;
        s.hp = s.stats.hp;
        s.done = false;
        s.carrying = s.carriedBy = -1;
        next.push_back(s);
    }
    roster = next;
    ++chapter;
    startChapter();
}

void Game::startChapter() {
    Level lv = makeLevel(chapter);
    map = lv.map;
    objective = lv.objective;
    chapterName = lv.name;
    units.clear();
    for (size_t i = 0; i < roster.size() && i < lv.starts.size(); ++i) {
        Unit u = roster[i];
        u.x = lv.starts[i].first;
        u.y = lv.starts[i].second;
        units.push_back(u);
    }
    units.insert(units.end(), lv.units.begin(), lv.units.end());
    for (auto& u : units) {
        u.drawX = static_cast<float>(u.x);
        u.drawY = static_cast<float>(u.y);
    }
    turn = 0;
    active = inspect = -1;
    pendingAttack = -1;
    sceneActive = false;
    showDanger = false;
    popups.clear();
    levelUps.clear();
    message.clear();
    int lord = lordIndex();
    cursorX = units[lord].x;
    cursorY = units[lord].y;
    beginPhase(Team::Player);
}

int Game::unitAt(int x, int y) const {
    for (size_t i = 0; i < units.size(); ++i)
        if (units[i].alive && units[i].carriedBy < 0 && units[i].x == x && units[i].y == y) return static_cast<int>(i);
    return -1;
}

int Game::lordIndex() const {
    for (size_t i = 0; i < units.size(); ++i)
        if (units[i].isLord) return static_cast<int>(i);
    return 0;
}

bool Game::teamAlive(Team t) const {
    return std::any_of(units.begin(), units.end(), [t](const Unit& u) { return u.alive && u.team == t; });
}

// Dijkstra over terrain move costs. Allies can be passed through; foes block.
Game::Reach Game::computeReach(int ui, int maxCost) const {
    const Unit& u = units[ui];
    int n = map.w * map.h;
    Reach r{std::vector<int>(n, -1), std::vector<int>(n, -1)};
    using Node = std::pair<int, int>;  // cost, tile
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
    int start = idx(u.x, u.y);
    r.cost[start] = 0;
    pq.push({0, start});
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!pq.empty()) {
        auto [c, t] = pq.top();
        pq.pop();
        if (c > r.cost[t]) continue;
        int tx = t % map.w, ty = t / map.w;
        for (const auto& d : dirs) {
            int nx = tx + d[0], ny = ty + d[1];
            if (!map.inBounds(nx, ny)) continue;
            int step = moveCost(map.at(nx, ny), u.moveType);
            if (step < 0) continue;
            int other = unitAt(nx, ny);
            if (other >= 0 && units[other].team != u.team) continue;
            int nc = c + step;
            if (nc > maxCost) continue;
            int ni = idx(nx, ny);
            if (r.cost[ni] < 0 || nc < r.cost[ni]) {
                r.cost[ni] = nc;
                r.prev[ni] = t;
                pq.push({nc, ni});
            }
        }
    }
    return r;
}

bool Game::standable(int ui, const Reach& r, int tile) const {
    if (r.cost[tile] < 0) return false;
    int other = unitAt(tile % map.w, tile / map.w);
    return other < 0 || other == ui;
}

std::vector<int> Game::pathTo(const Reach& r, int tile) const {
    std::vector<int> p;
    if (r.cost[tile] < 0) return p;
    for (int t = tile; t >= 0; t = r.prev[t]) p.push_back(t);
    std::reverse(p.begin(), p.end());
    return p;
}

void Game::computeRanges(int ui, std::vector<char>& move, std::vector<char>& attack) const {
    const Unit& u = units[ui];
    int n = map.w * map.h;
    move.assign(n, 0);
    attack.assign(n, 0);
    Reach r = computeReach(ui, u.ai == AIMode::Hold && u.team == Team::Enemy ? 0 : u.mov);
    const Weapon& w = u.weapon;
    for (int t = 0; t < n; ++t) {
        if (r.cost[t] < 0) continue;
        move[t] = 1;
        if (!standable(ui, r, t)) continue;
        int tx = t % map.w, ty = t / map.w;
        for (int dy = -w.maxRange; dy <= w.maxRange; ++dy) {
            for (int dx = -w.maxRange; dx <= w.maxRange; ++dx) {
                int d = std::abs(dx) + std::abs(dy);
                if (d < w.minRange || d > w.maxRange || !map.inBounds(tx + dx, ty + dy)) continue;
                attack[idx(tx + dx, ty + dy)] = 1;
            }
        }
    }
}

std::vector<int> Game::targetsFrom(int ui, int x, int y, ActionKind kind) const {
    const Unit& u = units[ui];
    std::vector<int> out;
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& t = units[i];
        if (!t.alive || t.carriedBy >= 0 || static_cast<int>(i) == ui) continue;
        int d = manhattan(x, y, t.x, t.y);
        if (d < u.weapon.minRange || d > u.weapon.maxRange) continue;
        if (kind == ActionKind::Attack && u.weapon.type != WeaponType::Staff && t.team != u.team)
            out.push_back(static_cast<int>(i));
        if (kind == ActionKind::Heal && u.weapon.type == WeaponType::Staff && t.team == u.team && t.hp < t.stats.hp)
            out.push_back(static_cast<int>(i));
    }
    // Rescue: any adjacent ally, as long as neither side is already carrying someone.
    if (kind == ActionKind::Rescue && u.carrying < 0) {
        for (size_t i = 0; i < units.size(); ++i) {
            const Unit& t = units[i];
            if (t.alive && t.carriedBy < 0 && t.carrying < 0 && static_cast<int>(i) != ui && t.team == u.team &&
                manhattan(x, y, t.x, t.y) == 1)
                out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

int Game::bestAttackTile(int ui, int target) const {
    const Unit& u = units[ui];
    const Unit& e = units[target];
    auto inRange = [&](int t) {
        int d = manhattan(t % map.w, t / map.w, e.x, e.y);
        return standable(ui, reach, t) && d >= u.weapon.minRange && d <= u.weapon.maxRange;
    };
    // Respect the route the player was hovering if it already ends in range.
    if (!path.empty() && inRange(path.back())) return path.back();
    // Otherwise: somewhere the enemy can't counter, then the best terrain, then the shortest walk.
    int best = -1, bestScore = INT_MIN;
    for (int t = 0; t < map.w * map.h; ++t) {
        if (!inRange(t)) continue;
        const TerrainInfo& ti = terrainInfo(map.at(t % map.w, t / map.w));
        Forecast f = computeForecast(map, u, t % map.w, t / map.w, e);
        int score = (f.def.canAttack ? 0 : 100000) + ti.def * 1000 + ti.avoid * 10 - reach.cost[t];
        if (score > bestScore) best = t, bestScore = score;
    }
    return best;
}

int Game::closestStandable(int ui, int x, int y) const {
    // Overshooting the move range lands on the reachable tile nearest the cursor; among equally near
    // tiles, the straighter line to the cursor wins, then the one that walks farther.
    int best = -1;
    float bestD = 1e9f;
    for (int t = 0; t < map.w * map.h; ++t) {
        if (!standable(ui, reach, t)) continue;
        int tx = t % map.w, ty = t / map.w;
        float d = manhattan(tx, ty, x, y) * 1000.0f + std::hypot(float(tx - x), float(ty - y)) * 10.0f - reach.cost[t] * 0.01f;
        if (d < bestD) best = t, bestD = d;
    }
    return best;
}

std::vector<int> Game::dropTiles(int ui) const {
    std::vector<int> out;
    const Unit& u = units[ui];
    if (u.carrying < 0) return out;
    static const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : dirs) {
        int x = u.x + d[0], y = u.y + d[1];
        if (map.inBounds(x, y) && unitAt(x, y) < 0 && moveCost(map.at(x, y), units[u.carrying].moveType) >= 0)
            out.push_back(idx(x, y));
    }
    return out;
}

void Game::rescue(int carrier, int ally) {
    units[carrier].carrying = ally;
    units[ally].carriedBy = carrier;
    units[ally].x = units[carrier].x;
    units[ally].y = units[carrier].y;
    addPopup(units[carrier].x, units[carrier].y, "Rescue!", {140, 220, 255, 255});
    message = units[carrier].name + " rescues " + units[ally].name + ".";
}

void Game::drop(int carrier, int tile) {
    int ally = units[carrier].carrying;
    if (ally < 0) return;
    Unit& a = units[ally];
    a.carriedBy = -1;
    a.x = tile % map.w;
    a.y = tile / map.w;
    a.drawX = static_cast<float>(a.x);
    a.drawY = static_cast<float>(a.y);
    a.done = true;  // a dropped unit has spent its turn
    units[carrier].carrying = -1;
}

float Game::strikeImpact() const { return sceneActive ? kSceneImpact : kStrikeImpact; }
float Game::strikeLength() const { return sceneActive ? kSceneStrike : 0.7f; }

Rectangle Game::menuRect() const {
    int ax = cursorX, ay = cursorY;
    if (menuKind == MenuKind::Action && active >= 0) {
        ax = units[active].x;
        ay = units[active].y;
    }
    float h = static_cast<float>(menu.size() * MENU_ITEM_H + 8);
    float x = ax < MAP_W / 2 ? (ax + 1) * TILE + 8.0f : ax * TILE - MENU_W - 8.0f;
    float y = std::clamp(ay * TILE - 4.0f, 4.0f, MAP_PX_H - h - 4.0f);
    return {x, y, static_cast<float>(MENU_W), h};
}

void Game::addPopup(int x, int y, std::string text, Color c) {
    Vector2 p = tileCenter(static_cast<float>(x), static_cast<float>(y));
    popups.push_back({std::move(text), p.x, p.y - 20, 0, c});
}

// ---------------------------------------------------------------------------
// Turn flow
// ---------------------------------------------------------------------------

void Game::beginPhase(Team t) {
    phase = t;
    if (t == Team::Player) ++turn;
    for (auto& u : units) {
        if (!u.alive || u.team != t) continue;
        u.done = false;
        if (u.carriedBy >= 0) continue;
        int heal = terrainInfo(map.at(u.x, u.y)).heal;
        if (heal > 0 && u.hp < u.stats.hp) {
            int amt = std::min(std::max(1, u.stats.hp * heal / 100), u.stats.hp - u.hp);
            u.hp += amt;
            addPopup(u.x, u.y, "+" + std::to_string(amt), kHealColor);
        }
    }
    active = inspect = -1;
    bannerText = t == Team::Player ? "PLAYER PHASE" : "ENEMY PHASE";
    bannerTimer = 1.3f;
    state = State::Banner;
}

void Game::afterAction() {
    active = -1;
    bool bossDead = std::any_of(units.begin(), units.end(), [](const Unit& u) { return u.isBoss && !u.alive; });
    if (bossDead || !teamAlive(Team::Enemy)) {
        state = State::Victory;
        return;
    }
    if (!units[lordIndex()].alive || !teamAlive(Team::Player)) {
        state = State::Defeat;
        return;
    }
    if (isAIPhase()) {
        state = State::AITurn;
        aiDelay = 0.3f;
        return;
    }
    bool allDone = std::all_of(units.begin(), units.end(),
                               [](const Unit& u) { return !u.alive || u.team != Team::Player || u.done || u.carriedBy >= 0; });
    if (allDone) beginPhase(Team::Enemy);
    else state = State::PlayerIdle;
}

void Game::selectUnit(int ui) {
    active = ui;
    inspect = -1;
    origX = units[ui].x;
    origY = units[ui].y;
    reach = computeReach(ui, units[ui].mov);
    computeRanges(ui, moveMask, attackMask);
    path = {idx(origX, origY)};
    state = State::Selected;
}

void Game::startMove(int ui, std::vector<int> p, bool byAI) {
    active = ui;
    movePath = std::move(p);
    moveT = 0;
    moveByAI = byAI;
    state = State::Moving;
    if (movePath.size() <= 1) finishMove();
}

void Game::finishMove() {
    Unit& u = units[active];
    if (!movePath.empty()) {
        u.x = movePath.back() % map.w;
        u.y = movePath.back() / map.w;
    }
    u.drawX = static_cast<float>(u.x);
    u.drawY = static_cast<float>(u.y);
    cursorX = u.x;
    cursorY = u.y;
    if (!moveByAI) {
        if (pendingAttack >= 0) {
            int e = pendingAttack;
            pendingAttack = -1;
            startCombat(active, e);
        } else {
            openActionMenu();
        }
        return;
    }
    if (aiTarget >= 0 && units[aiTarget].alive) {
        if (aiAction == ActionKind::Heal) startHeal(active, aiTarget);
        else startCombat(active, aiTarget);
    } else {
        u.done = true;
        afterAction();
    }
}

void Game::openActionMenu() {
    const Unit& u = units[active];
    menu.clear();
    if (!targetsFrom(active, u.x, u.y, ActionKind::Attack).empty()) menu.push_back("Attack");
    if (!targetsFrom(active, u.x, u.y, ActionKind::Heal).empty()) menu.push_back("Heal");
    if (!targetsFrom(active, u.x, u.y, ActionKind::Rescue).empty()) menu.push_back("Rescue");
    if (!dropTiles(active).empty()) menu.push_back("Drop");
    menu.push_back("Wait");
    menuIndex = 0;
    menuKind = MenuKind::Action;
    state = State::Menu;
}

void Game::openMapMenu() {
    menu = {"End Turn", showDanger ? "Hide Danger" : "Danger Zone", "Cancel"};
    menuIndex = 0;
    menuKind = MenuKind::Map;
    state = State::Menu;
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

void Game::startCombat(int a, int d) {
    combatA = a;
    combatD = d;
    combatIsHeal = false;
    Forecast f = computeForecast(map, units[a], units[a].x, units[a].y, units[d]);
    strikes = resolveCombat(f, units[a].hp, units[d].hp, rng);
    strikeIndex = 0;
    sceneActive = battleScenes;
    sceneT = 0;
    combatForecast = f;
    shownHp[0] = static_cast<float>(units[a].hp);
    shownHp[1] = static_cast<float>(units[d].hp);
    combatTimer = sceneActive ? -kSceneIntro : 0;  // negative: the scene's stand-off before strike one
    strikeApplied = false;
    landed[0] = landed[1] = false;
    message = units[a].name + " attacks " + units[d].name + "!";
    state = State::Combat;
}

void Game::startHeal(int h, int t) {
    combatA = h;
    combatD = t;
    combatIsHeal = true;
    Unit& target = units[t];
    int amt = std::min(units[h].stats.mag + 10, target.stats.hp - target.hp);
    target.hp += amt;
    addPopup(target.x, target.y, "+" + std::to_string(amt), kHealColor);
    message = units[h].name + " heals " + target.name + ".";
    combatTimer = 0;
    state = State::Combat;
}

void Game::applyStrike(const Strike& s) {
    Unit& target = s.byAttacker ? units[combatD] : units[combatA];
    target.hp = std::max(0, target.hp - s.damage);
    if (s.hit) landed[s.byAttacker ? 0 : 1] = true;
    if (!s.hit) addPopup(target.x, target.y, "MISS", {220, 220, 230, 255});
    else if (s.crit) addPopup(target.x, target.y, "CRIT! " + std::to_string(s.damage), {255, 200, 40, 255});
    else addPopup(target.x, target.y, std::to_string(s.damage), WHITE);
}

void Game::updateCombat(float dt) {
    combatTimer += dt;
    sceneT += dt;
    if (!combatIsHeal) {
        for (int k = 0; k < 2; ++k) {
            float target = static_cast<float>(units[k == 0 ? combatA : combatD].hp);
            float step = 40.0f * dt;  // HP per second the bars drain/fill
            shownHp[k] = shownHp[k] > target ? std::max(target, shownHp[k] - step) : std::min(target, shownHp[k] + step);
        }
    }
    if (combatIsHeal) {
        if (combatTimer >= 0.9f) endCombat();
        return;
    }
    if (strikeIndex < strikes.size()) {
        if (!strikeApplied && combatTimer >= strikeImpact()) {
            applyStrike(strikes[strikeIndex]);
            strikeApplied = true;
        }
        if (combatTimer >= strikeLength()) {
            ++strikeIndex;
            combatTimer = 0;
            strikeApplied = false;
        }
    } else if (combatTimer >= (sceneActive ? kSceneOutro : 0.4f)) {
        endCombat();
    }
}

int Game::expFor(const Unit& u, const Unit& foe, bool landedHit, bool killed) const {
    if (!landedHit) return 1;
    int diff = foe.level - u.level;
    int e = std::max(1, 10 + diff);
    if (killed) e = std::max(5, 30 + diff * 3) + (foe.isBoss ? 40 : 0);
    return std::min(e, 100);
}

void Game::gainExp(int ui, int amount) {
    Unit& u = units[ui];
    if (u.level >= 20) return;
    u.exp += amount;
    while (u.exp >= 100 && u.level < 20) {
        u.exp -= 100;
        ++u.level;
        LevelUpInfo info{ui, u.level, {}};
        std::uniform_int_distribution<int> rn(0, 99);
        for (int i = 0; i < kStatCount; ++i) {
            if (rn(rng) < u.growths.*kStatMembers[i]) {
                info.gains.*kStatMembers[i] = 1;
                u.stats.*kStatMembers[i] += 1;
            }
        }
        u.hp += info.gains.hp;
        if (u.level >= 20) u.exp = 0;
        levelUps.push_back(info);
    }
}

void Game::endCombat() {
    sceneActive = false;
    Unit& a = units[combatA];
    Unit& d = units[combatD];
    if (combatIsHeal) {
        if (a.team == Team::Player) gainExp(combatA, 11);
    } else {
        bool aDead = a.hp <= 0, dDead = d.hp <= 0;
        if (a.team == Team::Player && !aDead) gainExp(combatA, expFor(a, d, landed[0], dDead));
        if (d.team == Team::Player && !dDead) gainExp(combatD, expFor(d, a, landed[1], aDead));
        for (int ui : {combatA, combatD}) {
            Unit& u = units[ui];
            if (u.hp <= 0 && u.carrying >= 0) {
                units[u.carrying].carriedBy = -1;
                units[u.carrying].drawX = static_cast<float>(u.x);
                units[u.carrying].drawY = static_cast<float>(u.y);
                u.carrying = -1;
            }
        }
        if (aDead) {
            a.alive = false;
            message = a.name + " has fallen.";
        }
        if (dDead) {
            d.alive = false;
            message = d.name + " has fallen.";
        }
    }
    a.done = true;
    if (!levelUps.empty()) {
        levelTimer = 0;
        state = State::LevelUp;
    } else {
        afterAction();
    }
}

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------

void Game::runAI(int ui) {
    Unit& u = units[ui];
    active = ui;
    cursorX = u.x;
    cursorY = u.y;
    aiTarget = -1;
    Reach r = computeReach(ui, u.ai == AIMode::Hold ? 0 : u.mov);
    int n = map.w * map.h;

    if (u.weapon.type == WeaponType::Staff) {
        int bestTile = -1, bestMissing = 0;
        for (int t = 0; t < n; ++t) {
            if (!standable(ui, r, t)) continue;
            for (int target : targetsFrom(ui, t % map.w, t / map.w, ActionKind::Heal)) {
                int missing = units[target].stats.hp - units[target].hp;
                if (missing > bestMissing) {
                    bestMissing = missing;
                    bestTile = t;
                    aiTarget = target;
                }
            }
        }
        if (bestTile >= 0) {
            aiAction = ActionKind::Heal;
            startMove(ui, pathTo(r, bestTile), true);
        } else {
            u.done = true;
            afterAction();
        }
        return;
    }

    // Score every (tile, target) pair reachable this turn.
    double bestScore = -1e9;
    int bestTile = -1;
    for (int t = 0; t < n; ++t) {
        if (!standable(ui, r, t)) continue;
        int tx = t % map.w, ty = t / map.w;
        for (int target : targetsFrom(ui, tx, ty, ActionKind::Attack)) {
            const Unit& foe = units[target];
            Forecast f = computeForecast(map, u, tx, ty, foe);
            int atkHits = f.atk.doubles ? 2 : 1;
            int defHits = f.def.canAttack ? (f.def.doubles ? 2 : 1) : 0;
            double score = f.atk.damage * atkHits * f.atk.hit / 100.0;
            if (f.atk.damage * atkHits >= foe.hp) score += 40.0 * f.atk.hit / 100.0;
            score -= 0.5 * f.def.damage * defHits * f.def.hit / 100.0;
            score += terrainInfo(map.at(tx, ty)).avoid * 0.05;
            if (foe.isLord) score += 4;
            if (score > bestScore) {
                bestScore = score;
                bestTile = t;
                aiTarget = target;
            }
        }
    }
    if (bestTile >= 0) {
        aiAction = ActionKind::Attack;
        startMove(ui, pathTo(r, bestTile), true);
        return;
    }

    if (u.ai == AIMode::Charge) {
        // Find the cheapest tile (ignoring this turn's movement limit) to attack anyone from,
        // then walk as far along that path as this turn allows.
        Reach full = computeReach(ui, INT_MAX / 2);
        int goal = -1, goalCost = INT_MAX;
        for (int t = 0; t < n; ++t) {
            if (!standable(ui, full, t) || full.cost[t] >= goalCost) continue;
            if (!targetsFrom(ui, t % map.w, t / map.w, ActionKind::Attack).empty()) {
                goal = t;
                goalCost = full.cost[t];
            }
        }
        if (goal >= 0) {
            std::vector<int> p = pathTo(full, goal);
            size_t last = 0;
            for (size_t i = 0; i < p.size(); ++i)
                if (full.cost[p[i]] <= u.mov && standable(ui, full, p[i])) last = i;
            p.resize(last + 1);
            startMove(ui, p, true);
            return;
        }
    }
    u.done = true;
    afterAction();
}

void Game::updateAI(float dt) {
    aiDelay -= dt;
    if (aiDelay > 0) return;
    while (!aiQueue.empty() && (!units[aiQueue.front()].alive || units[aiQueue.front()].done))
        aiQueue.erase(aiQueue.begin());
    if (aiQueue.empty()) {
        beginPhase(phase == Team::Player ? Team::Enemy : Team::Player);
        return;
    }
    int ui = aiQueue.front();
    aiQueue.erase(aiQueue.begin());
    runAI(ui);
}

// ---------------------------------------------------------------------------
// Input and per-state updates
// ---------------------------------------------------------------------------

Game::Input Game::readInput() {
    Input in;
    // Gamepad: d-pad or left stick moves, A confirms (hold to fast-forward battles), B cancels, X toggles the
    // danger zone, Y toggles battle scenes, L/R cycle units. Buttons are by
    // position (A = right face, B = bottom face), matching the Nintendo layout printed on 8BitDo pads.
    int px = 0, py = 0;
    for (int g = 0; g < 4; ++g) {
        if (!IsGamepadAvailable(g)) continue;
        float ax = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(g, GAMEPAD_AXIS_LEFT_Y);
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f) px = -1;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f) px = 1;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.5f) py = -1;
        if (IsGamepadButtonDown(g, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.5f) py = 1;
        in.confirm |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        in.cancel |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        in.danger |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_UP);
        if (IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) in.cycle = 1;
        if (IsGamepadButtonPressed(g, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) in.cycle = -1;
        in.fast |= IsGamepadButtonDown(g, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        in.toggleScenes |= IsGamepadButtonPressed(g, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    }
    // Step once on press, then auto-repeat while held, like keyboard repeat.
    bool step = false;
    if (px != padHeldX || py != padHeldY) {
        step = px || py;
        padRepeat = 0.25f;
    } else if (px || py) {
        padRepeat -= GetFrameTime();
        if (padRepeat <= 0) {
            step = true;
            padRepeat += 0.07f;
        }
    }
    padHeldX = px;
    padHeldY = py;
    if (step) {
        in.dx = px;
        in.dy = py;
    }

    if (pressed(KEY_LEFT) || pressed(KEY_A)) in.dx = -1;
    if (pressed(KEY_RIGHT) || pressed(KEY_D)) in.dx = 1;
    if (pressed(KEY_UP) || pressed(KEY_W)) in.dy = -1;
    if (pressed(KEY_DOWN) || pressed(KEY_S)) in.dy = 1;
    in.confirm |= IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.cancel |= IsKeyPressed(KEY_X) || IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_ESCAPE) ||
                IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    in.danger |= IsKeyPressed(KEY_R);
    if (IsKeyPressed(KEY_E)) in.cycle = 1;
    if (IsKeyPressed(KEY_Q)) in.cycle = -1;
    in.fast |= IsKeyDown(KEY_Z) || IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.toggleScenes |= IsKeyPressed(KEY_T);
    in.mouse = GetMousePosition();
    Vector2 delta = GetMouseDelta();
    if ((delta.x != 0 || delta.y != 0) && in.mouse.x >= 0 && in.mouse.y >= 0 && in.mouse.x < MAP_PX_W &&
        in.mouse.y < MAP_PX_H) {
        in.mouseMoved = true;
        in.mx = static_cast<int>(in.mouse.x) / TILE;
        in.my = static_cast<int>(in.mouse.y) / TILE;
    }
    return in;
}

void Game::moveCursor(const Input& in) {
    if (in.mouseMoved) {
        cursorX = in.mx;
        cursorY = in.my;
    }
    cursorX = std::clamp(cursorX + in.dx, 0, map.w - 1);
    cursorY = std::clamp(cursorY + in.dy, 0, map.h - 1);
}

void Game::updatePlayerIdle(const Input& in) {
    moveCursor(in);
    if (in.danger) showDanger = !showDanger;
    if (in.cycle) {
        // Walk the roster from the unit under the cursor to the next player unit that hasn't acted.
        int n = static_cast<int>(units.size());
        int from = unitAt(cursorX, cursorY);
        if (from < 0) from = in.cycle > 0 ? n - 1 : 0;
        for (int k = 1; k <= n; ++k) {
            const Unit& u = units[((from + in.cycle * k) % n + n) % n];
            if (u.alive && u.team == Team::Player && !u.done && u.carriedBy < 0) {
                cursorX = u.x;
                cursorY = u.y;
                break;
            }
        }
    }
    if (inspect >= 0 && !units[inspect].alive) inspect = -1;
    if (in.cancel) inspect = -1;
    if (!in.confirm) return;
    int u = unitAt(cursorX, cursorY);
    if (u >= 0 && units[u].team == Team::Player && !units[u].done) {
        selectUnit(u);
    } else if (u >= 0 && units[u].team == Team::Enemy) {
        inspect = inspect == u ? -1 : u;
        if (inspect >= 0) computeRanges(inspect, moveMask, attackMask);
    } else if (u < 0) {
        openMapMenu();
    }
}

void Game::updateSelected(const Input& in) {
    moveCursor(in);
    int t = idx(cursorX, cursorY);
    // Hovering an enemy in range previews the walk to the tile we'd attack from.
    int foe = unitAt(cursorX, cursorY);
    bool canStrike = foe >= 0 && units[foe].team != units[active].team && attackMask[t] &&
                     units[active].weapon.type != WeaponType::Staff;
    int strikeTile = canStrike ? bestAttackTile(active, foe) : -1;
    int dest = strikeTile >= 0 ? strikeTile
               : standable(active, reach, t) ? t
                                             : closestStandable(active, cursorX, cursorY);
    if (dest >= 0) path = pathTo(reach, dest);
    if (in.cancel) {
        cursorX = origX;
        cursorY = origY;
        active = -1;
        state = State::PlayerIdle;
    } else if (in.confirm && strikeTile >= 0) {
        pendingAttack = foe;
        startMove(active, path, false);
    } else if (in.confirm && dest >= 0) {
        startMove(active, path, false);
    }
}

void Game::updateMenu(const Input& in) {
    int n = static_cast<int>(menu.size());
    if (in.dy) menuIndex = (menuIndex + in.dy + n) % n;
    Rectangle r = menuRect();
    bool mouseInMenu = CheckCollisionPointRec(in.mouse, r);
    if (mouseInMenu && in.mouseMoved) {
        int hovered = static_cast<int>((in.mouse.y - r.y - 4) / MENU_ITEM_H);
        if (hovered >= 0 && hovered < n) menuIndex = hovered;
    }

    if (in.cancel) {
        if (menuKind == MenuKind::Action) {
            // Undo the move and go back to choosing a destination.
            Unit& u = units[active];
            u.x = origX;
            u.y = origY;
            u.drawX = static_cast<float>(u.x);
            u.drawY = static_cast<float>(u.y);
            path = {idx(u.x, u.y)};
            state = State::Selected;
        } else {
            state = State::PlayerIdle;
        }
        return;
    }
    // A left click outside the menu shouldn't pick whatever item happens to be highlighted.
    if (!in.confirm || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseInMenu)) return;

    const std::string& choice = menu[menuIndex];
    if (choice == "Drop") {
        actionKind = ActionKind::Drop;
        targets = dropTiles(active);
        targetIndex = 0;
        cursorX = targets[0] % map.w;
        cursorY = targets[0] / map.w;
        state = State::Target;
    } else if (choice == "Attack" || choice == "Heal" || choice == "Rescue") {
        actionKind = choice == "Attack" ? ActionKind::Attack : choice == "Heal" ? ActionKind::Heal : ActionKind::Rescue;
        targets = targetsFrom(active, units[active].x, units[active].y, actionKind);
        targetIndex = 0;
        cursorX = units[targets[0]].x;
        cursorY = units[targets[0]].y;
        state = State::Target;
    } else if (choice == "Wait") {
        units[active].done = true;
        afterAction();
    } else if (choice == "End Turn") {
        beginPhase(Team::Enemy);
    } else if (choice == "Danger Zone" || choice == "Hide Danger") {
        showDanger = !showDanger;
        state = State::PlayerIdle;
    } else {
        state = State::PlayerIdle;
    }
}

void Game::updateTarget(const Input& in) {
    int n = static_cast<int>(targets.size());
    int step = in.dx + in.dy;
    if (step) targetIndex = (targetIndex + (step > 0 ? 1 : -1) + n) % n;
    // Targets are unit indices, except for Drop where they're tiles.
    bool tiles = actionKind == ActionKind::Drop;
    auto tx = [&](int i) { return tiles ? targets[i] % map.w : units[targets[i]].x; };
    auto ty = [&](int i) { return tiles ? targets[i] / map.w : units[targets[i]].y; };
    if (in.mouseMoved) {
        for (int i = 0; i < n; ++i)
            if (tx(i) == in.mx && ty(i) == in.my) targetIndex = i;
    }
    cursorX = tx(targetIndex);
    cursorY = ty(targetIndex);
    if (in.cancel) {
        cursorX = units[active].x;
        cursorY = units[active].y;
        openActionMenu();
    } else if (in.confirm) {
        if (actionKind == ActionKind::Attack) startCombat(active, targets[targetIndex]);
        else if (actionKind == ActionKind::Heal) startHeal(active, targets[targetIndex]);
        else if (actionKind == ActionKind::Rescue) {
            rescue(active, targets[targetIndex]);
            units[active].done = true;
            afterAction();
        } else {
            drop(active, targets[targetIndex]);
            units[active].done = true;
            afterAction();
        }
    }
}

void Game::update(float dt) {
    Input in = autoplay ? Input{} : readInput();

    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].carriedBy >= 0) {  // ride along with the carrier
            units[i].x = units[units[i].carriedBy].x;
            units[i].y = units[units[i].carriedBy].y;
        }
        if (state == State::Moving && static_cast<int>(i) == active) continue;
        units[i].drawX = static_cast<float>(units[i].x);
        units[i].drawY = static_cast<float>(units[i].y);
    }
    if (in.toggleScenes && state != State::Combat) {
        battleScenes = !battleScenes;
        message = battleScenes ? "Battle scenes: ON" : "Battle scenes: OFF";
    }
    if (state == State::Combat && sceneActive && in.fast) dt *= 3;
    for (auto& p : popups) p.t += dt;
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.t > 1.0f; }),
                 popups.end());

    switch (state) {
        case State::Banner:
            bannerTimer -= dt;
            if (bannerTimer <= 0 || in.confirm) {
                if (isAIPhase()) {
                    aiQueue.clear();
                    for (size_t i = 0; i < units.size(); ++i)
                        if (units[i].alive && units[i].team == phase) aiQueue.push_back(static_cast<int>(i));
                    aiDelay = 0.2f;
                    state = State::AITurn;
                } else {
                    int lord = lordIndex();
                    cursorX = units[lord].x;
                    cursorY = units[lord].y;
                    state = State::PlayerIdle;
                }
            }
            break;
        case State::PlayerIdle: updatePlayerIdle(in); break;
        case State::Selected: updateSelected(in); break;
        case State::Moving: {
            moveT += dt * 12.0f;
            if (moveT >= movePath.size() - 1) {
                finishMove();
            } else {
                int i = static_cast<int>(moveT);
                float f = moveT - i;
                int a = movePath[i], b = movePath[i + 1];
                units[active].drawX = (a % map.w) * (1 - f) + (b % map.w) * f;
                units[active].drawY = (a / map.w) * (1 - f) + (b / map.w) * f;
            }
            break;
        }
        case State::Menu: updateMenu(in); break;
        case State::Target: updateTarget(in); break;
        case State::Combat: updateCombat(dt); break;
        case State::LevelUp:
            levelTimer += dt;
            if ((in.confirm && levelTimer > 0.4f) || (autoplay && levelTimer > 1.0f)) {
                levelUps.erase(levelUps.begin());
                levelTimer = 0;
                if (levelUps.empty()) afterAction();
            }
            break;
        case State::AITurn: updateAI(dt); break;
        case State::Victory:
            if (in.confirm || (autoplay && !over())) {
                if (chapter + 1 < kChapterCount) advanceChapter();
                else newCampaign(0);
            }
            break;
        case State::Defeat:
            if (in.confirm) startChapter();
            break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Game::drawTile(int x, int y) const {
    Terrain t = map.at(x, y);
    const TerrainInfo& info = terrainInfo(t);
    float px = static_cast<float>(x * TILE), py = static_cast<float>(y * TILE);
    DrawRectangleRec({px, py, TILE, TILE}, info.color);
    switch (t) {
        case Terrain::Plain:
            DrawRectangleRec({px + 10, py + 14, 3, 3}, {100, 150, 70, 255});
            DrawRectangleRec({px + 32, py + 34, 3, 3}, {100, 150, 70, 255});
            break;
        case Terrain::Forest:
            for (auto [ox, oy] : {std::pair{14.0f, 8.0f}, std::pair{32.0f, 16.0f}, std::pair{18.0f, 26.0f}}) {
                DrawRectangleRec({px + ox - 2, py + oy + 14, 4, 6}, {90, 60, 40, 255});
                DrawTriangle({px + ox, py + oy}, {px + ox - 9, py + oy + 15}, {px + ox + 9, py + oy + 15},
                             {36, 92, 44, 255});
            }
            break;
        case Terrain::Mountain:
            DrawTriangle({px + 24, py + 6}, {px + 4, py + 42}, {px + 44, py + 42}, {116, 94, 70, 255});
            DrawTriangle({px + 24, py + 6}, {px + 18, py + 17}, {px + 30, py + 17}, {240, 240, 245, 255});
            break;
        case Terrain::Peak:
            DrawTriangle({px + 24, py + 2}, {px + 1, py + 46}, {px + 47, py + 46}, {92, 84, 80, 255});
            DrawTriangle({px + 24, py + 2}, {px + 13, py + 22}, {px + 35, py + 22}, {240, 240, 245, 255});
            DrawTriangle({px + 24, py + 2}, {px + 24, py + 46}, {px + 47, py + 46}, Fade(BLACK, 0.15f));
            break;
        case Terrain::Water: {
            float phase = static_cast<float>(std::fmod(GetTime() * 8.0, 12.0));
            for (int i = 0; i < 3; ++i) {
                float wy = py + 10 + i * 13;
                DrawLineEx({px + 6 + phase * 0.3f, wy}, {px + 22 + phase * 0.3f, wy}, 2, {130, 170, 230, 255});
            }
            break;
        }
        case Terrain::Wall:
            for (int row = 0; row < 4; ++row) {
                DrawLineEx({px, py + row * 12.0f}, {px + TILE, py + row * 12.0f}, 1, {70, 70, 80, 255});
                float off = row % 2 ? 12.0f : 0.0f;
                for (float bx = off; bx < TILE; bx += 24)
                    DrawLineEx({px + bx, py + row * 12.0f}, {px + bx, py + row * 12.0f + 12}, 1, {70, 70, 80, 255});
            }
            break;
        case Terrain::Fort:
            DrawRectangleLinesEx({px + 6, py + 10, 36, 32}, 3, {120, 96, 64, 255});
            DrawRectangleRec({px + 20, py + 26, 8, 16}, {120, 96, 64, 255});
            DrawLineEx({px + 24, py + 10}, {px + 24, py + 2}, 2, {80, 60, 40, 255});
            DrawTriangle({px + 25, py + 2}, {px + 25, py + 8}, {px + 33, py + 5}, {220, 60, 60, 255});
            break;
        case Terrain::Throne:
            DrawRectangleRec({px + 14, py + 6, 20, 24}, {200, 160, 50, 255});
            DrawRectangleRec({px + 10, py + 24, 28, 16}, {170, 40, 60, 255});
            break;
        case Terrain::Road:
            for (int row = 0; row < 3; ++row) {
                float off = row % 2 ? 8.0f : 0.0f;
                for (float bx = off; bx < TILE; bx += 16)
                    DrawRectangleLinesEx({px + bx, py + row * 16.0f, 16, 16}, 1, {132, 124, 108, 255});
            }
            break;
        case Terrain::Rubble:
            for (auto [ox, oy, r] : {std::tuple{12.0f, 30.0f, 8.0f}, std::tuple{30.0f, 34.0f, 6.0f},
                                     std::tuple{26.0f, 16.0f, 7.0f}, std::tuple{38.0f, 12.0f, 4.0f}}) {
                DrawPoly({px + ox, py + oy}, 5, r, ox * 7, {104, 96, 88, 255});
                DrawPoly({px + ox - 1, py + oy - 2}, 5, r * 0.6f, ox * 7, {150, 140, 128, 255});
            }
            DrawLineEx({px + 6, py + 14}, {px + 18, py + 10}, 3, {96, 70, 50, 255});  // broken beam
            break;
    }
    DrawRectangleLinesEx({px, py, TILE, TILE}, 1, Fade(BLACK, 0.08f));
}

void Game::drawHighlights() const {
    auto fillMask = [&](const std::vector<char>& mask, Color c, float alpha) {
        for (size_t t = 0; t < mask.size(); ++t) {
            if (!mask[t]) continue;
            float px = static_cast<float>((t % map.w) * TILE), py = static_cast<float>((t / map.w) * TILE);
            DrawRectangleRec({px + 1, py + 1, TILE - 2, TILE - 2}, Fade(c, alpha));
        }
    };

    bool playerTurnIdle = state == State::PlayerIdle || state == State::Selected;
    if (showDanger && playerTurnIdle) {
        std::vector<char> danger(map.w * map.h, 0), move, attack;
        for (size_t i = 0; i < units.size(); ++i) {
            if (!units[i].alive || units[i].team != Team::Enemy) continue;
            computeRanges(static_cast<int>(i), move, attack);
            for (size_t t = 0; t < danger.size(); ++t) danger[t] |= attack[t];
        }
        fillMask(danger, {170, 40, 200, 255}, 0.28f);
    }

    if (state == State::Selected || (state == State::PlayerIdle && inspect >= 0)) {
        int ui = state == State::Selected ? active : inspect;
        bool heals = units[ui].weapon.type == WeaponType::Staff;
        std::vector<char> outer = attackMask;
        for (size_t t = 0; t < outer.size(); ++t)
            if (moveMask[t]) outer[t] = 0;
        fillMask(moveMask, kMoveColor, 0.45f);
        fillMask(outer, heals ? kHealColor : kAttackColor, 0.4f);
    }

    if (state == State::Target && actionKind == ActionKind::Drop) {
        std::vector<char> spots(map.w * map.h, 0);
        for (int t : targets) spots[t] = 1;
        fillMask(spots, kMoveColor, 0.45f);
    } else if (state == State::Target) {
        std::vector<char> range(map.w * map.h, 0);
        const Unit& u = units[active];
        for (int t = 0; t < map.w * map.h; ++t) {
            int d = manhattan(u.x, u.y, t % map.w, t / map.w);
            range[t] = actionKind == ActionKind::Rescue ? d == 1 : d >= u.weapon.minRange && d <= u.weapon.maxRange;
        }
        fillMask(range, actionKind == ActionKind::Attack ? kAttackColor : actionKind == ActionKind::Heal ? kHealColor : kMoveColor, 0.35f);
    }

    if (state == State::Selected && path.size() > 1) {
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            Vector2 a = tileCenter(static_cast<float>(path[i] % map.w), static_cast<float>(path[i] / map.w));
            Vector2 b = tileCenter(static_cast<float>(path[i + 1] % map.w), static_cast<float>(path[i + 1] / map.w));
            DrawLineEx(a, b, 8, {255, 230, 120, 220});
            DrawCircleV(b, 4, {255, 230, 120, 220});
        }
        Vector2 end = tileCenter(static_cast<float>(path.back() % map.w), static_cast<float>(path.back() / map.w));
        DrawCircleV(end, 9, {255, 210, 80, 240});
    }
}

Game::AnimPick Game::pickAnim(int ui) const {
    const Unit& u = units[ui];
    if (state == State::Moving && ui == active) return {"walk", static_cast<float>(GetTime())};
    if (state == State::Combat && (ui == combatA || ui == combatD)) {
        if (combatIsHeal) {
            if (ui == combatA) return {"attack", combatTimer};
        } else if (strikeIndex < strikes.size() && combatTimer >= 0) {
            const Strike& s = strikes[strikeIndex];
            if (ui == (s.byAttacker ? combatA : combatD)) return {"attack", combatTimer};
            if (strikeApplied) {
                float t = combatTimer - strikeImpact();
                if (!s.hit) return {"dodge", t};
                return {u.hp <= 0 ? "death" : "hurt", t};
            }
        }
    }
    if (u.hp <= 0) return {"death", 99.0f};
    return {"idle", static_cast<float>(GetTime()) + ui * 0.37f};  // offset so idles don't bob in sync
}

bool Game::facingLeft(int ui) const {
    const Unit& u = units[ui];
    if (state == State::Moving && ui == active && movePath.size() > 1) {
        // Face along the most recent horizontal step of the path.
        size_t k = std::min(static_cast<size_t>(moveT), movePath.size() - 2);
        for (size_t j = k + 1; j-- > 0;) {
            int dx = movePath[j + 1] % map.w - movePath[j] % map.w;
            if (dx) return dx < 0;
        }
    }
    if (state == State::Combat && (ui == combatA || ui == combatD)) {
        const Unit& o = units[ui == combatA ? combatD : combatA];
        if (o.x != u.x) return o.x < u.x;
    }
    return u.team == Team::Enemy;
}

bool Game::drawUnitSprite(int ui, Vector2 feet, float scale, bool faceLeft, Color tint, bool battle) const {
    auto sprite = sprites.find(units[ui].className);
    if (sprite == sprites.end()) return false;
    const Sprite& sp = sprite->second;

    // Pick the frame: requested animation, falling back toward idle when it's missing or finished.
    int row = 0, col = 0;
    AnimPick pick = pickAnim(ui);
    std::string name = pick.name;
    if (!sp.anims.count(name) && name == "death") name = "hurt";
    bool loop = name == "idle" || name == "walk";
    auto anim = sp.anims.find(name);
    if (anim != sp.anims.end()) {
        const Anim& a = anim->second;
        int f = std::max(0, static_cast<int>(pick.t / frameTime(name)));
        if (name == "attack" && a.hit > 0 && !combatIsHeal) {
            // Spread the wind-up so the hit frame appears exactly as damage lands.
            float impact = strikeImpact();
            f = pick.t < impact ? static_cast<int>(pick.t / (impact / a.hit))
                                : a.hit + static_cast<int>((pick.t - impact) / frameTime(name));
        }
        if (loop) f %= a.frames;
        else if (f >= a.frames && pick.name == std::string("death")) f = a.frames - 1;
        else if (f >= a.frames) anim = sp.anims.end();  // finished: drop back to idle
        if (anim != sp.anims.end()) row = a.row, col = a.cols.empty() ? f : a.cols[f];
    }
    if (anim == sp.anims.end() && sp.anims.count("idle")) {
        const Anim& a = sp.anims.at("idle");
        float t = static_cast<float>(GetTime()) + ui * 0.37f;
        int f = static_cast<int>(t / frameTime("idle")) % a.frames;
        row = a.row;
        col = a.cols.empty() ? f : a.cols[f];
    }
    if (battle) scale *= sp.battleScale;

    bool flip = faceLeft != sp.facesLeft;
    float cw = static_cast<float>(sp.cellW), ch = static_cast<float>(sp.cellH);
    float ax = flip ? cw - sp.anchorX : static_cast<float>(sp.anchorX);
    DrawTexturePro(battle ? sp.battleTex : sp.tex, {col * cw, row * ch, flip ? -cw : cw, ch},
                   {feet.x - ax * scale, feet.y - sp.feetY * scale, cw * scale, ch * scale}, {0, 0}, 0, tint);
    return true;
}

void Game::drawUnits() const {
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!u.alive || u.carriedBy >= 0) continue;
        Vector2 c = tileCenter(u.drawX, u.drawY);

        // Lunge toward the opponent during the strike wind-up.
        if (state == State::Combat && !combatIsHeal && strikeIndex < strikes.size()) {
            const Strike& s = strikes[strikeIndex];
            int striker = s.byAttacker ? combatA : combatD;
            int victim = s.byAttacker ? combatD : combatA;
            auto sp = sprites.find(u.className);
            bool animated = sp != sprites.end() && sp->second.anims.count("attack");
            if (static_cast<int>(i) == striker && combatTimer < 0.35f && !animated) {
                float k = std::sin(combatTimer / 0.35f * PI) * 0.3f;
                c.x += (units[victim].x - u.x) * TILE * k / std::max(1, manhattan(u.x, u.y, units[victim].x, units[victim].y));
                c.y += (units[victim].y - u.y) * TILE * k / std::max(1, manhattan(u.x, u.y, units[victim].x, units[victim].y));
            }
            if (static_cast<int>(i) == victim && strikeApplied && s.hit && combatTimer < 0.45f) {
                c.x += std::sin(combatTimer * 90.0f) * 3.0f;
            }
        }

        Color body = u.team == Team::Player ? kPlayerColor : kEnemyColor;
        if (u.done && phase == u.team) body = u.team == Team::Player ? Color{88, 96, 118, 255} : Color{120, 84, 84, 255};
        float alpha = u.hp <= 0 ? 0.35f : 1.0f;

        auto sprite = sprites.find(u.className);
        if (sprite != sprites.end()) {
            const Sprite& sp = sprite->second;
            if (u.hp <= 0 && sp.anims.count("death")) alpha = 0.85f;
            // Team-colored base under the feet.
            int ex = static_cast<int>(c.x), ey = static_cast<int>(c.y + 16);
            DrawEllipse(ex, ey, 18, 6, Fade(body, 0.7f * alpha));
            if (u.isLord || u.isBoss) {
                Color ring = u.isLord ? GOLD : Color{40, 20, 20, 255};
                for (int k = 0; k < 2; ++k) DrawEllipseLines(ex, ey, 18.0f + k, 6.0f + k, Fade(ring, alpha));
            } else {
                DrawEllipseLines(ex, ey, 18, 6, Fade(BLACK, 0.5f * alpha));
            }

            Color tint = u.done && phase == u.team ? Color{130, 130, 140, 255} : WHITE;
            drawUnitSprite(static_cast<int>(i), {c.x, c.y + 19}, kSpriteScale, facingLeft(static_cast<int>(i)),
                           Fade(tint, alpha));
            if (u.isBoss) {
                float top = c.y + 19 - 112 * kSpriteScale - 2;
                DrawTriangle({c.x - 10, top + 8}, {c.x - 4, top + 6}, {c.x - 10, top}, GOLD);
                DrawTriangle({c.x - 4, top + 6}, {c.x + 4, top + 6}, {c.x, top - 2}, GOLD);
                DrawTriangle({c.x + 4, top + 6}, {c.x + 10, top + 8}, {c.x + 10, top}, GOLD);
            }
            drawHpBar(c.x - 18, c.y + 19, 36, 4, u.hp, u.stats.hp);
            if (u.carrying >= 0) {  // rescue badge: a small team-colored tag with the carried unit's initial
                Rectangle tag{c.x + 10, c.y - 26, 14, 14};
                DrawRectangleRec(tag, u.team == Team::Player ? kPlayerColor : kEnemyColor);
                DrawRectangleLinesEx(tag, 1.5f, WHITE);
                char initial[2] = {units[u.carrying].name[0], 0};
                drawTextCentered(initial, static_cast<int>(tag.x + 7), static_cast<int>(tag.y + 2), 10, WHITE);
            }
            continue;
        }

        DrawEllipse(static_cast<int>(c.x), static_cast<int>(c.y + 16), 16, 5, Fade(BLACK, 0.25f * alpha));
        if (u.moveType == MoveType::Flier) {
            DrawTriangle({c.x - 14, c.y - 4}, {c.x - 28, c.y - 12}, {c.x - 18, c.y + 6}, Fade(WHITE, 0.9f * alpha));
            DrawTriangle({c.x + 14, c.y - 4}, {c.x + 18, c.y + 6}, {c.x + 28, c.y - 12}, Fade(WHITE, 0.9f * alpha));
        }
        DrawCircleV(c, 17, Fade(body, alpha));
        DrawCircleLinesV(c, 17, Fade(BLACK, 0.5f * alpha));
        if (u.isLord) DrawRing(c, 17, 20, 0, 360, 32, Fade(GOLD, alpha));
        if (u.isBoss) {
            DrawRing(c, 17, 20, 0, 360, 32, Fade({40, 20, 20, 255}, alpha));
            DrawTriangle({c.x - 10, c.y - 18}, {c.x - 4, c.y - 20}, {c.x - 10, c.y - 26}, GOLD);
            DrawTriangle({c.x - 4, c.y - 20}, {c.x + 4, c.y - 20}, {c.x, c.y - 28}, GOLD);
            DrawTriangle({c.x + 4, c.y - 20}, {c.x + 10, c.y - 18}, {c.x + 10, c.y - 26}, GOLD);
        }
        std::string glyph = u.className.substr(0, 2);
        drawTextCentered(glyph.c_str(), static_cast<int>(c.x), static_cast<int>(c.y - 8), 16, Fade(WHITE, alpha));
        drawHpBar(c.x - 18, c.y + 19, 36, 4, u.hp, u.stats.hp);
    }

    for (const auto& p : popups) {
        float a = 1.0f - p.t;
        int size = 22;
        int w = MeasureText(p.text.c_str(), size);
        int x = static_cast<int>(p.x) - w / 2, y = static_cast<int>(p.y - p.t * 28);
        DrawText(p.text.c_str(), x + 2, y + 2, size, Fade(BLACK, a));
        DrawText(p.text.c_str(), x, y, size, Fade(p.color, a));
    }
}

void Game::drawCursor() const {
    bool visible = state == State::PlayerIdle || state == State::Selected || state == State::Target;
    if (!visible) return;
    float pulse = 2.0f + 2.0f * static_cast<float>(std::sin(GetTime() * 6.0));
    float x = cursorX * TILE - pulse, y = cursorY * TILE - pulse, s = TILE + pulse * 2, l = 12;
    Color c = state == State::Target ? Color{255, 90, 90, 255} : Color{255, 230, 90, 255};
    for (auto [cx, cy, sx, sy] : {std::tuple{x, y, 1.f, 1.f}, std::tuple{x + s, y, -1.f, 1.f},
                                  std::tuple{x, y + s, 1.f, -1.f}, std::tuple{x + s, y + s, -1.f, -1.f}}) {
        DrawLineEx({cx, cy}, {cx + l * sx, cy}, 4, c);
        DrawLineEx({cx, cy}, {cx, cy + l * sy}, 4, c);
    }
}

void Game::drawMenu() const {
    if (state != State::Menu) return;
    Rectangle r = menuRect();
    DrawRectangleRounded(r, 0.15f, 6, {24, 30, 60, 235});
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, 2, {180, 190, 230, 255});
    for (size_t i = 0; i < menu.size(); ++i) {
        float y = r.y + 4 + i * MENU_ITEM_H;
        if (static_cast<int>(i) == menuIndex)
            DrawRectangleRounded({r.x + 4, y, r.width - 8, MENU_ITEM_H - 2.0f}, 0.3f, 6, {80, 110, 200, 255});
        DrawText(menu[i].c_str(), static_cast<int>(r.x + 14), static_cast<int>(y + 6), 18, kText);
    }
}

void Game::drawCombatHud() const {
    if (state != State::Combat || combatIsHeal || sceneActive) return;
    const Unit& a = units[combatA];
    const Unit& d = units[combatD];
    float h = 74;
    float y = (a.y >= MAP_H / 2) ? 8.0f : MAP_PX_H - h - 8.0f;
    Rectangle box{40, y, MAP_PX_W - 80.0f, h};
    DrawRectangleRounded(box, 0.15f, 6, Fade({16, 18, 28, 255}, 0.9f));
    auto side = [&](const Unit& u, float x, Color c) {
        DrawText(u.name.c_str(), static_cast<int>(x), static_cast<int>(y + 10), 20, c);
        drawHpBar(x, y + 40, 220, 12, u.hp, u.stats.hp);
        DrawText(TextFormat("%d / %d", u.hp, u.stats.hp), static_cast<int>(x + 230), static_cast<int>(y + 37), 18, kText);
    };
    Color ca = a.team == Team::Player ? Color{120, 170, 255, 255} : Color{255, 120, 120, 255};
    Color cd = d.team == Team::Player ? Color{120, 170, 255, 255} : Color{255, 120, 120, 255};
    side(a, box.x + 20, ca);
    side(d, box.x + box.width / 2 + 20, cd);
}

void Game::drawUnitInfo(const Unit& u, int x, int y) const {
    Color nameColor = u.team == Team::Player ? Color{130, 175, 255, 255} : Color{255, 130, 130, 255};
    DrawText(u.name.c_str(), x, y, 24, nameColor);
    DrawText(TextFormat("%s  Lv %d", u.className.c_str(), u.level), x, y + 28, 16, kDim);
    if (u.team == Team::Player) DrawText(TextFormat("EXP %d", u.exp), x + 180, y + 28, 16, kDim);

    DrawText(TextFormat("HP %d/%d", u.hp, u.stats.hp), x, y + 54, 18, kText);
    drawHpBar(static_cast<float>(x + 120), static_cast<float>(y + 58), 120, 10, u.hp, u.stats.hp);

    int sy = y + 84;
    for (int i = 1; i < kStatCount; ++i) {
        int col = (i - 1) % 2, row = (i - 1) / 2;
        int sx = x + col * 120;
        DrawText(kStatNames[i], sx, sy + row * 22, 18, kDim);
        DrawText(TextFormat("%d", u.stats.*kStatMembers[i]), sx + 50, sy + row * 22, 18, kText);
    }
    DrawText("Mov", x + 120, sy + 3 * 22, 18, kDim);
    DrawText(TextFormat("%d", u.mov), x + 170, sy + 3 * 22, 18, kText);

    int wy = sy + 4 * 22 + 8;
    const Weapon& w = u.weapon;
    DrawText(TextFormat("%s (%s)", w.name.c_str(), weaponTypeName(w.type)), x, wy, 18, {240, 220, 150, 255});
    if (w.type == WeaponType::Staff) {
        DrawText(TextFormat("Heals %d HP   Rng 1", u.stats.mag + 10), x, wy + 22, 16, kDim);
    } else {
        DrawText(TextFormat("Mt %d  Hit %d  Crt %d  Wt %d", w.might, w.hit, w.crit, w.weight), x, wy + 22, 16, kDim);
        if (w.minRange == w.maxRange) DrawText(TextFormat("Rng %d", w.minRange), x, wy + 42, 16, kDim);
        else DrawText(TextFormat("Rng %d-%d", w.minRange, w.maxRange), x, wy + 42, 16, kDim);
        if (w.effective) DrawText("Effective bonus", x + 80, wy + 42, 16, {255, 170, 90, 255});
    }
    if (u.carrying >= 0)
        DrawText(TextFormat("Carrying %s (Skl/Spd halved)", units[u.carrying].name.c_str()), x, wy + 66, 16,
                 {140, 220, 255, 255});
}

void Game::drawForecast(int x, int y) const {
    const Unit& a = units[active];
    if (actionKind == ActionKind::Drop) {
        DrawText("DROP", x, y, 18, {255, 220, 120, 255});
        if (a.carrying >= 0) DrawText(units[a.carrying].name.c_str(), x, y + 34, 22, {130, 175, 255, 255});
        DrawText("Choose a tile to set them down", x, y + 64, 16, kDim);
        return;
    }
    if (actionKind == ActionKind::Rescue) {
        DrawText("RESCUE", x, y, 18, {255, 220, 120, 255});
        DrawText(units[targets[targetIndex]].name.c_str(), x, y + 34, 22, {130, 175, 255, 255});
        DrawText("Carry them (Skl/Spd halved while carrying)", x, y + 64, 16, kDim);
        return;
    }
    const Unit& d = units[targets[targetIndex]];
    DrawText(actionKind == ActionKind::Heal ? "HEAL" : "COMBAT FORECAST", x, y, 18, {255, 220, 120, 255});
    if (actionKind == ActionKind::Heal) {
        int amt = std::min(a.stats.mag + 10, d.stats.hp - d.hp);
        DrawText(d.name.c_str(), x, y + 34, 22, {130, 175, 255, 255});
        DrawText(TextFormat("HP %d -> %d", d.hp, d.hp + amt), x, y + 64, 20, kText);
        return;
    }
    Forecast f = computeForecast(map, a, a.x, a.y, d);
    int c1 = x + 70, c2 = x + 170;
    DrawText(a.name.c_str(), c1, y + 30, 18, {130, 175, 255, 255});
    DrawText(d.name.c_str(), c2, y + 30, 18, {255, 130, 130, 255});
    auto row = [&](const char* label, const char* va, const char* vd, int ry) {
        DrawText(label, x, ry, 18, kDim);
        DrawText(va, c1, ry, 20, kText);
        DrawText(vd, c2, ry, 20, kText);
    };
    auto val = [](const SideStats& s, int v) { return s.canAttack ? TextFormat("%d", v) : "--"; };
    row("HP", TextFormat("%d", a.hp), TextFormat("%d", d.hp), y + 58);
    std::string dmgA = std::string(val(f.atk, f.atk.damage)) + (f.atk.doubles ? " x2" : "");
    std::string dmgD = std::string(val(f.def, f.def.damage)) + (f.def.doubles ? " x2" : "");
    row("Dmg", dmgA.c_str(), dmgD.c_str(), y + 84);
    std::string hitA = val(f.atk, f.atk.hit), hitD = val(f.def, f.def.hit);
    row("Hit", hitA.c_str(), hitD.c_str(), y + 110);
    std::string crA = val(f.atk, f.atk.crit), crD = val(f.def, f.def.crit);
    row("Crit", crA.c_str(), crD.c_str(), y + 136);

    int tri = triangleBonus(a.weapon.type, d.weapon.type);
    if (tri > 0) DrawText("Weapon advantage!", x, y + 170, 16, {120, 230, 140, 255});
    if (tri < 0) DrawText("Weapon disadvantage", x, y + 170, 16, {240, 120, 120, 255});
    if (a.weapon.effective & bit(d.moveType)) DrawText("Effective damage!", x, y + 190, 16, {255, 170, 90, 255});
}

void Game::drawPanel() const {
    int px = MAP_PX_W;
    DrawRectangle(px, 0, PANEL_W, MAP_PX_H, kPanelBg);
    DrawLine(px, 0, px, MAP_PX_H, kPanelLine);
    int x = px + 16;

    Color phaseColor = phase == Team::Player ? Color{120, 170, 255, 255} : Color{255, 120, 120, 255};
    DrawText(TextFormat("Turn %d", turn), x, 14, 22, kText);
    DrawText(phase == Team::Player ? "Player Phase" : "Enemy Phase", x + 110, 18, 18, phaseColor);
    DrawText(TextFormat("Goal: %s", objective.c_str()), x, 44, 16, kDim);

    const TerrainInfo& ti = terrainInfo(map.at(cursorX, cursorY));
    DrawRectangle(x, 70, PANEL_W - 32, 30, {36, 40, 54, 255});
    DrawRectangle(x + 6, 76, 18, 18, ti.color);
    DrawText(ti.name, x + 32, 76, 18, kText);
    std::string tstats = TextFormat("Def %d  Avo %d", ti.def, ti.avoid);
    DrawText(tstats.c_str(), x + PANEL_W - 40 - MeasureText(tstats.c_str(), 16), 78, 16, kDim);

    int infoY = 118;
    if (state == State::Target) {
        drawForecast(x, infoY);
    } else {
        int u = unitAt(cursorX, cursorY);
        if (u < 0 && active >= 0) u = active;
        if (u >= 0) drawUnitInfo(units[u], x, infoY);
    }

    int alivePlayers = 0, aliveEnemies = 0;
    for (const auto& u : units) {
        if (!u.alive) continue;
        (u.team == Team::Player ? alivePlayers : aliveEnemies)++;
    }
    DrawText(TextFormat("Allies %d   Enemies %d", alivePlayers, aliveEnemies), x, MAP_PX_H - 30, 16, kDim);
}

void Game::drawBottomBar() const {
    int y = MAP_PX_H;
    DrawRectangle(0, y, SCREEN_W, BAR_H, {18, 19, 26, 255});
    DrawLine(0, y, SCREEN_W, y, kPanelLine);
    const char* help = "";
    switch (state) {
        case State::PlayerIdle: help = "Arrows/WASD/Mouse: move   Z/Enter/Click: select   X: cancel   R: danger zone   T: battle scenes"; break;
        case State::Selected: help = "Choose a destination or an enemy to attack   Z: move   X: cancel"; break;
        case State::Combat: help = sceneActive ? "Hold Z: fast-forward" : ""; break;
        case State::Menu: help = "Up/Down: choose   Z: confirm   X: back"; break;
        case State::Target: help = "Arrows: cycle targets   Z: confirm   X: back"; break;
        case State::LevelUp: help = "Z: continue"; break;
        case State::Victory: help = over() ? "Z/Enter: play again" : "Z/Enter: next chapter"; break;
        case State::Defeat: help = "Z/Enter: retry chapter"; break;
        default: break;
    }
    DrawText(help, 16, y + 10, 18, kDim);
    DrawText(message.c_str(), 16, y + 36, 18, kText);
}

void Game::drawLevelUp() const {
    if (state != State::LevelUp || levelUps.empty()) return;
    const LevelUpInfo& info = levelUps.front();
    const Unit& u = units[info.unit];
    Rectangle r{MAP_PX_W / 2.0f - 150, MAP_PX_H / 2.0f - 150, 300, 300};
    DrawRectangle(0, 0, MAP_PX_W, MAP_PX_H, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.08f, 8, {24, 30, 60, 245});
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, 2, {255, 220, 120, 255});
    drawTextCentered("LEVEL UP!", static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + 14), 28, {255, 220, 120, 255});
    drawTextCentered(TextFormat("%s  Lv %d", u.name.c_str(), info.level), static_cast<int>(r.x + r.width / 2),
                     static_cast<int>(r.y + 48), 20, kText);
    for (int i = 0; i < kStatCount; ++i) {
        int col = i % 2, row = i / 2;
        int sx = static_cast<int>(r.x + 30 + col * 130), sy = static_cast<int>(r.y + 90 + row * 46);
        int gain = info.gains.*kStatMembers[i];
        DrawText(kStatNames[i], sx, sy, 20, kDim);
        DrawText(TextFormat("%d", u.stats.*kStatMembers[i]), sx + 46, sy, 20, kText);
        if (gain) DrawText("+1", sx + 80, sy, 20, {120, 240, 140, 255});
    }
}

void Game::drawCenterBanner(const char* text, Color c, const char* sub) const {
    float cy = MAP_PX_H / 2.0f;
    DrawRectangle(0, static_cast<int>(cy - 44), MAP_PX_W, 88, Fade(BLACK, 0.6f));
    drawTextCentered(text, MAP_PX_W / 2, static_cast<int>(cy - 26), 44, c);
    if (sub) drawTextCentered(sub, MAP_PX_W / 2, static_cast<int>(cy + 22), 18, kText);
}

void Game::drawBattleScene() const {
    if (state != State::Combat || !sceneActive) return;
    constexpr float kW = SCREEN_W, kH = MAP_PX_H, kGround = 300, kFeetY = 338, kScale = 1.6f;
    constexpr float kHomeL = 300, kHomeR = 740;

    // Fade in as it opens, out once the last strike has settled.
    float vis = std::clamp(sceneT / 0.25f, 0.0f, 1.0f);
    if (strikeIndex >= strikes.size()) vis *= 1 - std::clamp((combatTimer - (kSceneOutro - 0.25f)) / 0.25f, 0.0f, 1.0f);
    auto fade = [&](Color c, float a = 1) { return Fade(c, a * vis); };

    // GBA layout: the player's unit stands on the right facing left, the enemy on the left facing right.
    int right = units[combatA].team == Team::Player ? combatA : combatD;
    int left = right == combatA ? combatD : combatA;

    DrawRectangleGradientV(0, 0, static_cast<int>(kW), static_cast<int>(kGround), fade({34, 44, 86, 255}),
                           fade({126, 156, 206, 255}));
    for (int i = 0; i < 7; ++i) {  // distant hills
        float hx = i * 180.0f - 60, hw = 260;
        DrawEllipse(static_cast<int>(hx + hw / 2), static_cast<int>(kGround), hw / 2, 60 + (i % 3) * 18.0f,
                    fade({70, 96, 120, 255}));
    }
    auto shade = [](Color c, float k) {
        return Color{static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k),
                     static_cast<unsigned char>(c.b * k), 255};
    };
    for (int side = 0; side < 2; ++side) {
        const Unit& u = units[side == 0 ? left : right];
        Color g = terrainInfo(map.at(u.x, u.y)).color;
        float x0 = side == 0 ? 0 : kW / 2, home = side == 0 ? kHomeL : kHomeR;
        DrawRectangleGradientV(static_cast<int>(x0), static_cast<int>(kGround), static_cast<int>(kW / 2),
                               static_cast<int>(kH - kGround), fade(shade(g, 0.8f)), fade(shade(g, 0.45f)));
        DrawEllipse(static_cast<int>(home), static_cast<int>(kFeetY), 130, 24, fade(g));
        DrawEllipseLines(static_cast<int>(home), static_cast<int>(kFeetY), 130, 24, fade(BLACK, 0.35f));
        DrawEllipse(static_cast<int>(home), static_cast<int>(kFeetY), 50, 9, fade(BLACK, 0.25f));
    }

    // Where each fighter stands this frame, and what the current strike is doing.
    bool striking = strikeIndex < strikes.size() && combatTimer >= 0;
    const Strike* s = striking ? &strikes[strikeIndex] : nullptr;
    int striker = s ? (s->byAttacker ? combatA : combatD) : -1;
    int victim = s ? (s->byAttacker ? combatD : combatA) : -1;
    float t = combatTimer, impact = kSceneImpact;
    bool melee = manhattan(units[combatA].x, units[combatA].y, units[combatD].x, units[combatD].y) <= 1;
    auto homeOf = [&](int ui) { return ui == left ? kHomeL : kHomeR; };
    auto dirOf = [&](int ui) { return ui == left ? 1.0f : -1.0f; };  // toward the opponent
    auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };

    float fx[2];
    int who[2] = {left, right};
    for (int k = 0; k < 2; ++k) {
        int ui = who[k];
        float x = homeOf(ui);
        if (s && ui == striker && melee) {
            // Dash in for the blow, then walk back.
            float k1 = smooth(t / (impact - 0.05f)) * (1 - smooth((t - impact - 0.25f) / 0.3f));
            x += dirOf(ui) * (kHomeR - kHomeL - 150) * k1;
        }
        if (s && ui == victim && strikeApplied) {
            if (s->hit) x += std::sin(t * 80.0f) * 6.0f * (1 - smooth((t - impact) / 0.3f));
            else x -= dirOf(ui) * 40 * std::sin(std::clamp((t - impact + 0.1f) / 0.4f, 0.0f, 1.0f) * PI);  // sidestep
        }
        fx[k] = x;
    }

    for (int k = 0; k < 2; ++k) {
        int ui = who[k];
        const Unit& u = units[ui];
        float alpha = 1;
        auto sp = sprites.find(u.className);
        bool deathAnim = sp != sprites.end() && sp->second.anims.count("death");
        if (u.hp <= 0 && !deathAnim) alpha = s ? 1 - smooth((t - impact - 0.2f) / 0.5f) : 0;
        Vector2 feet{fx[k], kFeetY};
        if (!drawUnitSprite(ui, feet, kScale, ui == right, fade(WHITE, alpha), true)) {
            Color body = u.team == Team::Player ? kPlayerColor : kEnemyColor;
            DrawCircleV({feet.x, feet.y - 60}, 40, fade(body, alpha));
        }
        if (s && ui == victim && s->hit && strikeApplied && t < impact + 0.12f) {
            BeginBlendMode(BLEND_ADDITIVE);  // flash white on the hit
            drawUnitSprite(ui, feet, kScale, ui == right, fade(WHITE, 0.8f * alpha), true);
            EndBlendMode();
        }
    }

    if (s) {
        float vx = fx[victim == left ? 0 : 1], sx = fx[striker == left ? 0 : 1];
        float chest = kFeetY - 90, dir = dirOf(striker);
        // Ranged attacks: a projectile flies over in the last moments before impact.
        if (!melee && t >= impact - 0.3f && t < impact) {
            float p = (t - (impact - 0.3f)) / 0.3f;
            float px = sx + dir * 40 + ((vx - dir * 30) - (sx + dir * 40)) * p;
            float py = chest - std::sin(p * PI) * 30;
            switch (units[striker].weapon.type) {
                case WeaponType::Bow:
                    DrawLineEx({px - dir * 34, py + 4}, {px, py}, 3, fade({150, 110, 70, 255}));
                    DrawTriangle({px + dir * 8, py - 1}, {px, py - 5}, {px, py + 4}, fade({220, 220, 230, 255}));
                    DrawTriangle({px + dir * 8, py - 1}, {px, py + 4}, {px, py - 5}, fade({220, 220, 230, 255}));
                    break;
                case WeaponType::Tome:
                    DrawCircleV({px, py}, 20, fade({255, 140, 40, 255}, 0.35f));
                    DrawCircleV({px, py}, 11, fade({255, 190, 60, 255}));
                    DrawCircleV({px, py}, 5, fade({255, 250, 210, 255}));
                    break;
                default:  // thrown lance / axe
                    DrawLineEx({px - dir * 60, py + 6}, {px, py}, 4, fade({140, 110, 80, 255}));
                    DrawCircleV({px, py}, 4, fade({220, 220, 230, 255}));
                    break;
            }
        }
        if (strikeApplied) {
            float since = t - impact;
            if (s->hit && since < 0.15f) {  // impact spark
                for (int r = 0; r < 8; ++r) {
                    float ang = r * PI / 4 + 0.3f, len = 24 + since * 200;
                    DrawLineEx({vx + std::cos(ang) * 10, chest + std::sin(ang) * 10},
                               {vx + std::cos(ang) * len, chest + std::sin(ang) * len}, 3, fade({255, 240, 180, 255}));
                }
            }
            if (s->crit && since < 0.2f) DrawRectangle(0, 0, static_cast<int>(kW), static_cast<int>(kH), fade(WHITE, 0.8f * (1 - since / 0.2f)));
            if (since < 0.8f) {
                const char* text = !s->hit ? "MISS" : TextFormat("%d", s->damage);
                Color c = !s->hit ? Color{220, 220, 230, 255} : s->crit ? Color{255, 200, 40, 255} : WHITE;
                int size = s->crit ? 44 : 36, y = static_cast<int>(chest - 120 - since * 40);
                float a = 1 - smooth((since - 0.5f) / 0.3f);
                int w = MeasureText(text, size);
                DrawText(text, static_cast<int>(vx) - w / 2 + 3, y + 3, size, fade(BLACK, a));
                DrawText(text, static_cast<int>(vx) - w / 2, y, size, fade(c, a));
                if (s->crit && s->hit) drawTextCentered("CRITICAL!", static_cast<int>(vx), y - 34, 26, fade({255, 120, 60, 255}, a));
            }
        }
    }

    // HUD: one panel per side with name, weapon, HIT/DMG/CRT, and an HP bar that drains.
    for (int k = 0; k < 2; ++k) {
        int ui = who[k];
        const Unit& u = units[ui];
        bool isAtk = ui == combatA;
        const SideStats& st = isAtk ? combatForecast.atk : combatForecast.def;
        float shown = shownHp[isAtk ? 0 : 1];
        Rectangle r{k == 0 ? 12.0f : kW / 2 + 6, 384, kW / 2 - 18, kH - 392};
        Color base = u.team == Team::Player ? Color{28, 48, 112, 255} : Color{112, 30, 42, 255};
        DrawRectangleRounded(r, 0.1f, 6, fade(base, 0.95f));
        DrawRectangleRoundedLinesEx(r, 0.1f, 6, 3, fade({235, 225, 190, 255}));
        int x = static_cast<int>(r.x + 18), y = static_cast<int>(r.y + 12);
        DrawText(u.name.c_str(), x, y, 28, fade(kText));
        DrawText(u.weapon.name.c_str(), x, y + 34, 18, fade({240, 220, 150, 255}));
        auto stat = [&](const char* label, std::string v, int col) {
            int cx = x + 260 + col * 76;
            DrawText(label, cx, y + 2, 16, fade(kDim));
            DrawText(v.c_str(), cx, y + 22, 24, fade(kText));
        };
        auto num = [&](int v) { return st.canAttack ? std::to_string(v) : std::string("--"); };
        stat("HIT", num(st.hit), 0);
        stat("DMG", num(st.damage) + (st.canAttack && st.doubles ? "x2" : ""), 1);
        stat("CRT", num(st.crit), 2);
        int hp = static_cast<int>(std::ceil(shown));
        DrawText(TextFormat("HP %2d", hp), x, y + 70, 28, fade(kText));
        if (vis > 0.99f) drawHpBar(static_cast<float>(x + 110), static_cast<float>(y + 76), r.width - 150, 16, hp, u.stats.hp);
    }
}

void Game::draw() const {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, kPanelBg);  // not ClearBackground: main letterboxes around us
    for (int y = 0; y < map.h; ++y)
        for (int x = 0; x < map.w; ++x) drawTile(x, y);
    drawHighlights();
    drawUnits();
    drawCursor();
    drawMenu();
    drawCombatHud();
    drawLevelUp();

    if (state == State::Banner) {
        Color c = phase == Team::Player ? Color{120, 170, 255, 255} : Color{255, 110, 110, 255};
        bool opening = turn == 1 && phase == Team::Player;
        drawCenterBanner(bannerText.c_str(), c, opening ? chapterName.c_str() : nullptr);
    } else if (state == State::Victory) {
        drawCenterBanner(over() ? "VICTORY" : "CHAPTER CLEAR", {255, 220, 120, 255},
                         TextFormat("Cleared on turn %d", turn));
    } else if (state == State::Defeat) {
        drawCenterBanner("GAME OVER", {255, 90, 90, 255}, "Rowan has fallen...");
    }

    drawPanel();
    drawBattleScene();
    drawBottomBar();
}
