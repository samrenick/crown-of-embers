#include "combat.h"
#include <algorithm>
#include <cstdlib>

int triangleBonus(WeaponType a, WeaponType b) {
    auto beats = [](WeaponType x, WeaponType y) {
        return (x == WeaponType::Sword && y == WeaponType::Axe) ||
               (x == WeaponType::Axe && y == WeaponType::Lance) ||
               (x == WeaponType::Lance && y == WeaponType::Sword);
    };
    if (beats(a, b)) return 1;
    if (beats(b, a)) return -1;
    return 0;
}

static int attackSpeed(const Unit& u) {
    return u.stats.spd - std::max(0, u.weapon.weight - u.stats.str);
}

static SideStats sideStats(const Unit& u, const Unit& foe, const TerrainInfo& foeTerrain, int dist) {
    SideStats s;
    s.attackSpeed = attackSpeed(u);
    const Weapon& w = u.weapon;
    s.canAttack = w.type != WeaponType::Staff && dist >= w.minRange && dist <= w.maxRange;
    if (!s.canAttack) return s;

    int tri = triangleBonus(w.type, foe.weapon.type);
    bool magic = w.type == WeaponType::Tome;
    int might = w.might * ((w.effective & bit(foe.moveType)) ? 3 : 1);
    int power = (magic ? u.stats.mag : u.stats.str) + might + tri;
    int defense = (magic ? foe.stats.res : foe.stats.def) + foeTerrain.def;
    s.damage = std::max(0, power - defense);

    int hit = w.hit + u.stats.skl * 2 + u.stats.lck / 2 + tri * 15;
    int avoid = attackSpeed(foe) * 2 + foe.stats.lck + foeTerrain.avoid;
    s.hit = std::clamp(hit - avoid, 0, 100);
    s.crit = std::clamp(w.crit + u.stats.skl / 2 - foe.stats.lck, 0, 100);
    return s;
}

// A unit carrying a rescued ally fights at half Skill and Speed.
static Unit burdened(const Unit& u) {
    Unit c = u;
    if (u.carrying >= 0) {
        c.stats.skl /= 2;
        c.stats.spd /= 2;
    }
    return c;
}

Forecast computeForecast(const Map& map, const Unit& atk, int ax, int ay, const Unit& def) {
    Unit a = burdened(atk), d = burdened(def);
    int dist = std::abs(ax - d.x) + std::abs(ay - d.y);
    Forecast f;
    f.atk = sideStats(a, d, terrainInfo(map.at(d.x, d.y)), dist);
    f.def = sideStats(d, a, terrainInfo(map.at(ax, ay)), dist);
    f.atk.doubles = f.atk.canAttack && f.atk.attackSpeed - f.def.attackSpeed >= 4;
    f.def.doubles = f.def.canAttack && f.def.attackSpeed - f.atk.attackSpeed >= 4;
    return f;
}

std::vector<Strike> resolveCombat(const Forecast& f, int atkHp, int defHp, std::mt19937& rng) {
    std::uniform_int_distribution<int> rn(0, 99);
    std::vector<Strike> out;
    int hp[2] = {atkHp, defHp};
    const SideStats* side[2] = {&f.atk, &f.def};

    auto strike = [&](int who) {
        const SideStats& s = *side[who];
        if (hp[0] <= 0 || hp[1] <= 0 || !s.canAttack) return;
        // "True hit": averaging two rolls makes high rates more reliable and low rates rarer.
        bool hit = (rn(rng) + rn(rng)) / 2 < s.hit;
        bool crit = hit && rn(rng) < s.crit;
        int dmg = hit ? s.damage * (crit ? 3 : 1) : 0;
        hp[1 - who] = std::max(0, hp[1 - who] - dmg);
        out.push_back({who == 0, hit, crit, dmg});
    };

    strike(0);
    strike(1);
    if (f.atk.doubles) strike(0);
    if (f.def.doubles) strike(1);
    return out;
}
