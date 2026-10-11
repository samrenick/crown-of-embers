// Converts a WAV file to QOA (raylib's compact, gapless format) for the game's music.
// Usage: qoaconv in.wav out.qoa
#include "raylib.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: qoaconv in.wav out.qoa\n");
        return 1;
    }
    SetTraceLogLevel(LOG_WARNING);
    Wave w = LoadWave(argv[1]);
    if (!IsWaveValid(w)) return 1;
    bool ok = ExportWave(w, argv[2]);
    UnloadWave(w);
    return ok ? 0 : 1;
}
