#include "game.h"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Usage: fe [--autoplay] [--speed N] [--screenshot out.png --frames N] [--scale S]
//   --autoplay    AI controls both armies (handy for smoke-testing combat/AI)
//   --screenshot  save a PNG after N frames and exit
//   --scale       initial window scale (default: as large as fits, in half steps up to 2x)
//   --battle-shot with --screenshot: capture T seconds into the first battle scene instead of at a frame
//   --chapter     start at chapter N (1-based) with the starting army
//   --hide-panel  start with the side panel collapsed (Tab toggles it in game)
int main(int argc, char** argv) {
    bool autoplay = false;
    float speed = 1.0f;
    const char* shotPath = nullptr;
    int shotFrame = 60;
    float startScale = 0;  // 0 = pick from monitor size
    float battleShot = -1;
    int chapter = 0;
    bool hidePanel = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--autoplay")) autoplay = true;
        else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc) speed = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) shotFrame = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) startScale = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--battle-shot") && i + 1 < argc) battleShot = std::strtof(argv[++i], nullptr);
        else if (!std::strcmp(argv[i], "--hide-panel")) hidePanel = true;
        else if (!std::strcmp(argv[i], "--chapter") && i + 1 < argc) chapter = std::atoi(argv[++i]) - 1;
    }

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(SCREEN_W, SCREEN_H, "Crown of Embers");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    SetWindowMinSize(MAP_W * TILE / 2, SCREEN_H / 2);

    // Open as large as the monitor comfortably allows. Screenshots default to 1x so they stay comparable.
    int monitor = GetCurrentMonitor();
    int mw = GetMonitorWidth(monitor), mh = GetMonitorHeight(monitor);
    if (startScale <= 0) {
        float fit = std::min(mw * 0.9f / SCREEN_W, mh * 0.85f / SCREEN_H);
        startScale = shotPath ? 1.0f : std::clamp(std::floor(fit * 2) / 2, 1.0f, 2.0f);
    }
    if (startScale != 1.0f) {
        int w = static_cast<int>(SCREEN_W * startScale), h = static_cast<int>(SCREEN_H * startScale);
        SetWindowSize(w, h);
        SetWindowPosition(std::max(0, (mw - w) / 2), std::max(0, (mh - h) / 2));
    }

    // Test runs (autoplay, screenshots) stay silent.
    if (!autoplay && !shotPath) InitAudioDevice();

    {
        // Scoped so the Game (and its textures and sounds) is destroyed before the window closes.
        Game game(autoplay, chapter);
        if (hidePanel) game.hidePanel();
        int frame = 0;
        int viewW = game.viewWidth();
        while (!WindowShouldClose()) {
            float dt = GetFrameTime();
            if (dt > 0.05f) dt = 0.05f;
            game.update(dt * speed);

            // F / F11 / gamepad Select toggles fullscreen (borderless, so it's instant and keeps the desktop mode).
            bool toggle = IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11);
            for (int g = 0; g < 4; ++g) toggle |= IsGamepadAvailable(g) && IsGamepadButtonPressed(g, GAMEPAD_BUTTON_MIDDLE_LEFT);
            if (toggle) ToggleBorderlessWindowed();

            // Hiding/showing the side panel changes the view width; in a window, resize to match at the same scale.
            if (game.viewWidth() != viewW) {
                float s = std::min(GetScreenWidth() / static_cast<float>(viewW), GetScreenHeight() / static_cast<float>(SCREEN_H));
                viewW = game.viewWidth();
                if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) && !IsWindowFullscreen() && !IsWindowMaximized())
                    SetWindowSize(static_cast<int>(viewW * s), GetScreenHeight());
            }

            // The game draws in fixed viewW x SCREEN_H coordinates; scale that to fit the window and
            // letterbox the rest. Mouse input is mapped back into game coordinates.
            float scale = std::min(GetScreenWidth() / static_cast<float>(viewW),
                                   GetScreenHeight() / static_cast<float>(SCREEN_H));
            float ox = std::floor((GetScreenWidth() - viewW * scale) / 2);
            float oy = std::floor((GetScreenHeight() - SCREEN_H * scale) / 2);
            SetMouseOffset(static_cast<int>(-ox), static_cast<int>(-oy));
            SetMouseScale(1 / scale, 1 / scale);

            BeginDrawing();
            ClearBackground(BLACK);
            rlPushMatrix();
            rlTranslatef(ox, oy, 0);
            rlScalef(scale, scale, 1);
            game.draw();
            rlPopMatrix();
            // Mask the letterbox so nothing drawn outside the view (e.g. battle-scene hills) shows in the bars.
            int vx1 = static_cast<int>(ox + viewW * scale), vy1 = static_cast<int>(oy + SCREEN_H * scale);
            DrawRectangle(0, 0, static_cast<int>(ox), GetScreenHeight(), BLACK);
            DrawRectangle(vx1, 0, GetScreenWidth() - vx1, GetScreenHeight(), BLACK);
            DrawRectangle(0, 0, GetScreenWidth(), static_cast<int>(oy), BLACK);
            DrawRectangle(0, vy1, GetScreenWidth(), GetScreenHeight() - vy1, BLACK);
            ++frame;
            bool shoot = battleShot >= 0 ? game.battleClock() >= battleShot : frame == shotFrame;
            if (shotPath && shoot) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, shotPath);
                UnloadImage(img);
                EndDrawing();
                break;
            }
            EndDrawing();

            if (autoplay && game.over()) {
                std::printf("%s on turn %d\n", game.won() ? "Victory" : "Defeat", game.turnNumber());
                break;
            }
            if (autoplay && game.turnNumber() > 60) {
                std::printf("Stalemate after 60 turns\n");
                break;
            }
        }
    }
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    return 0;
}
