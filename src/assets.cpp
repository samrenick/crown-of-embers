#include "assets.h"
#include "raylib.h"

std::string findAsset(const std::string& relPath) {
    // Look next to the working directory first (make run), then beside the binary (build/fe),
    // then inside a macOS app bundle (Contents/MacOS/fe -> Contents/Resources/assets).
    std::string dir = GetApplicationDirectory();
    for (std::string path : {"assets/" + relPath, dir + "../assets/" + relPath, dir + "../Resources/assets/" + relPath})
        if (FileExists(path.c_str())) return path;
    return "";
}
