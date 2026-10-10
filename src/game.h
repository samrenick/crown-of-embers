#pragma once
#include "combat.h"
#include "level.h"
#include <map>
#include <random>
#include <string>
#include <vector>

constexpr int TILE = 48;
constexpr int MAP_W = 16, MAP_H = 11;
constexpr int PANEL_W = 272, BAR_H = 64;
constexpr int SCREEN_W = MAP_W * TILE + PANEL_W;
constexpr int SCREEN_H = MAP_H * TILE + BAR_H;

class Game {
public:
    explicit Game(bool autoplay);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
    void update(float dt);
    void draw() const;

    bool over() const { return state == State::Victory || state == State::Defeat; }
    bool won() const { return state == State::Victory; }
    int turnNumber() const { return turn; }
    // Seconds into the current battle scene, or -1 when none is showing (used for debug screenshots).
    float battleClock() const { return sceneActive && state == State::Combat ? sceneT : -1; }

private:
    enum class State { Banner, PlayerIdle, Selected, Moving, Menu, Target, Combat, LevelUp, AITurn, Victory, Defeat };
    enum class MenuKind { Action, Map };
    enum class ActionKind { Attack, Heal };

    struct Reach {
        std::vector<int> cost, prev;  // cost -1 = unreachable
    };
    struct Input {
        int dx = 0, dy = 0;
        bool confirm = false, cancel = false, danger = false;
        int cycle = 0;  // +1 / -1: jump to the next / previous unit that can still act
        bool fast = false;          // confirm held: fast-forward battle scenes
        bool toggleScenes = false;  // turn battle scenes on/off
        bool mouseMoved = false;
        int mx = -1, my = -1;  // hovered map tile
        Vector2 mouse{};
    };
    struct Popup {
        std::string text;
        float x, y, t;
        Color color;
    };
    struct LevelUpInfo {
        int unit, level;
        Stats gains;
    };

    void reset();
    void loadSprites();

    // queries
    int idx(int x, int y) const { return y * map.w + x; }
    int unitAt(int x, int y) const;
    int lordIndex() const;
    bool teamAlive(Team t) const;
    bool isAIPhase() const { return phase == Team::Enemy || autoplay; }
    Reach computeReach(int ui, int maxCost) const;
    bool standable(int ui, const Reach& r, int tile) const;
    std::vector<int> pathTo(const Reach& r, int tile) const;
    void computeRanges(int ui, std::vector<char>& move, std::vector<char>& attack) const;
    std::vector<int> targetsFrom(int ui, int x, int y, ActionKind kind) const;
    Rectangle menuRect() const;
    int bestAttackTile(int ui, int target) const;
    int closestStandable(int ui, int x, int y) const;
    float strikeImpact() const;  // seconds into a strike when damage lands
    float strikeLength() const;

    // flow
    void beginPhase(Team t);
    void afterAction();
    void selectUnit(int ui);
    void startMove(int ui, std::vector<int> movePath, bool byAI);
    void finishMove();
    void openActionMenu();
    void openMapMenu();
    void startCombat(int a, int d);
    void startHeal(int h, int t);
    void applyStrike(const Strike& s);
    void endCombat();
    int expFor(const Unit& u, const Unit& foe, bool landedHit, bool killed) const;
    void gainExp(int ui, int amount);
    void runAI(int ui);
    void addPopup(int x, int y, std::string text, Color c);

    // per-state input handling
    Input readInput();
    void moveCursor(const Input& in);
    void updatePlayerIdle(const Input& in);
    void updateSelected(const Input& in);
    void updateMenu(const Input& in);
    void updateTarget(const Input& in);
    void updateCombat(float dt);
    void updateAI(float dt);

    // drawing
    void drawTile(int x, int y) const;
    void drawHighlights() const;
    void drawUnits() const;
    bool drawUnitSprite(int ui, Vector2 feet, float scale, bool faceLeft, Color tint) const;
    void drawBattleScene() const;
    void drawCursor() const;
    void drawMenu() const;
    void drawCombatHud() const;
    void drawPanel() const;
    void drawUnitInfo(const Unit& u, int x, int y) const;
    void drawForecast(int x, int y) const;
    void drawBottomBar() const;
    void drawLevelUp() const;
    void drawCenterBanner(const char* text, Color c, const char* sub) const;

    struct Anim {
        int row, frames;
        int hit = -1;  // attack only: frame shown when damage lands; frames before it stretch to fit the wind-up
    };
    struct Sprite {
        Texture2D tex;
        bool facesLeft;  // art drawn facing left; flipped so player units face right
        // Cell layout within tex, in texture pixels. A static sprite is one cell covering the whole texture.
        int cellW = 0, cellH = 0, anchorX = 0, feetY = 0;
        std::map<std::string, Anim> anims;  // idle, walk, attack, hurt, dodge, death (any may be missing)
    };
    struct AnimPick {
        const char* name;
        float t;  // seconds since the animation started
    };
    AnimPick pickAnim(int ui) const;
    bool facingLeft(int ui) const;
    std::map<std::string, Sprite> sprites;  // keyed by className; classes without one draw as tokens

    Map map;
    std::vector<Unit> units;
    std::string objective;
    std::mt19937 rng;
    bool autoplay;

    State state = State::Banner;
    Team phase = Team::Player;
    int turn = 0;
    int cursorX = 0, cursorY = 0;
    int active = -1;   // unit currently acting
    int inspect = -1;  // enemy whose range is being previewed
    int origX = 0, origY = 0;
    Reach reach;
    std::vector<char> moveMask, attackMask;
    std::vector<int> path;
    bool showDanger = false;

    std::vector<int> movePath;
    float moveT = 0;
    bool moveByAI = false;

    MenuKind menuKind = MenuKind::Action;
    std::vector<std::string> menu;
    int menuIndex = 0;
    ActionKind actionKind = ActionKind::Attack;
    std::vector<int> targets;
    int targetIndex = 0;

    int pendingAttack = -1;  // enemy to attack as soon as the current player move finishes
    bool battleScenes = true;  // show the GBA-style battle screen for attacks
    bool sceneActive = false;  // the current combat is using it
    float sceneT = 0;          // seconds since the battle scene opened
    Forecast combatForecast{};
    float shownHp[2] = {0, 0};  // attacker/defender HP as drawn; eases toward the real value

    int combatA = -1, combatD = -1;
    bool combatIsHeal = false;
    std::vector<Strike> strikes;
    size_t strikeIndex = 0;
    float combatTimer = 0;
    bool strikeApplied = false;
    bool landed[2] = {false, false};

    std::vector<LevelUpInfo> levelUps;
    float levelTimer = 0;

    std::vector<int> aiQueue;
    float aiDelay = 0;
    int aiTarget = -1;
    ActionKind aiAction = ActionKind::Attack;

    std::vector<Popup> popups;
    std::string bannerText;
    float bannerTimer = 0;
    int padHeldX = 0, padHeldY = 0;  // gamepad direction currently held, for auto-repeat
    float padRepeat = 0;
    std::string message;
};
