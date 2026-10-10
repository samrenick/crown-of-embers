#pragma once
#include "map.h"
#include <random>
#include <vector>

struct SideStats {
    int damage = 0, hit = 0, crit = 0, attackSpeed = 0;
    bool canAttack = false, doubles = false;
};

struct Forecast {
    SideStats atk, def;
};

// +1 if a has weapon-triangle advantage over b, -1 if disadvantage, else 0.
int triangleBonus(WeaponType a, WeaponType b);

// Forecast for attacker `a` standing at (ax, ay) attacking `d` where it stands.
Forecast computeForecast(const Map& map, const Unit& a, int ax, int ay, const Unit& d);

struct Strike {
    bool byAttacker, hit, crit;
    int damage;
};

// Rolls the full exchange (attack, counter, follow-ups), stopping when someone dies.
std::vector<Strike> resolveCombat(const Forecast& f, int atkHp, int defHp, std::mt19937& rng);
