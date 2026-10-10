# Crown of Embers

A small grid-based tactics game in C++17 with [raylib](https://www.raylib.com/).

## Build and run

```sh
brew install raylib
make run
```

(There's also a `CMakeLists.txt` if you prefer CMake 3.16+.)

To get a double-clickable macOS app instead:

```sh
make app       # builds "build/Crown of Embers.app" (raylib built in, so it runs without Homebrew)
make install   # copies it into /Applications
```

Debug flags:

- `./build/fe --autoplay --speed 20` — AI plays both sides; prints the result and exits
- `./build/fe --screenshot out.png --frames 120` — saves a PNG after N frames
- `./build/fe --scale 1.5` — opens the window at a given scale
- `./build/fe --chapter 2` — starts at a given chapter with the starting army
- `./build/fe --autoplay --screenshot out.png --battle-shot 1.2` — saves a PNG 1.2s into the first battle scene

## Controls

| Key | Gamepad | Action |
| --- | --- | --- |
| Arrows / WASD / mouse | D-pad / left stick | Move cursor |
| Z / Enter / Space / left click | A (right face button) | Select, confirm |
| X / Backspace / Esc / right click | B (bottom face button) | Cancel, undo move |
| R | X (top face button) | Toggle enemy danger zone |
| Q / E | L / R shoulder | Previous / next unit that hasn't acted |
| F / F11 | Select | Toggle fullscreen |
| T | Y (left face button) | Toggle battle scenes |
| Hold Z | Hold A | Fast-forward a battle scene |

The window opens as large as your screen allows and can be resized; the game scales to fit.

While moving a unit, pick an enemy in range to walk to the best tile and attack it in one go. Select an enemy to preview its movement and attack range. Select an empty tile to open the map menu (End Turn).

## Chapters

1. **The Border Fields**: open country with a walled keep. Defeat Vargus on the throne.
2. **The Ashen City**: an abandoned city cut by a canal. Streets are fast, rubble slows you down but gives cover, and Corvin holds the throne of a ruined cathedral.

Survivors carry over between chapters with their levels and EXP. Losing restarts the current chapter.

## Mechanics

- **Movement:** Dijkstra over per-terrain costs that vary by movement type (infantry, armored, cavalry, flier). Units can pass through allies but not enemies.
- **Combat:** GBA-style formulas, including the weapon triangle (+15 hit, +1 damage), doubling when attack speed is 4+ higher, crits at 3x damage, and terrain defense and avoid. Hit rolls average two random numbers (the "true hit" system).
- **Effective weapons:** bows deal triple might against fliers; the Rapier deals triple might against armored and cavalry units.
- **Rescue:** move a unit onto an ally to pick them up. The carrier fights at half Skill and Speed, and the carried unit can't be targeted. Use **Drop** from the action menu to set them down on an adjacent tile. If the carrier falls, the carried unit is dropped where it stood.
- **Healing:** staff healing, plus forts and the throne restore HP at the start of each phase.
- **Progression:** EXP and level ups with per-stat growth rates. Death is permanent; the game is lost if Rowan dies.
- **Enemy AI:** Charge, InRange, or Hold behavior. Attacks are scored by expected damage, kill chance, and counter risk.

## Layout

- `src/types.h`: units, weapons, stats
- `src/map.*`: terrain table, movement costs
- `src/combat.*`: forecast and combat resolution
- `src/level.*`: chapter maps, enemy placements, and the starting army
- `src/game.*`: state machine, AI, rendering
- `src/main.cpp`: window, main loop, debug flags
