#pragma once
#include <string>

// Full path to a file under assets/ (e.g. "sprites/lord.png"), or "" if it can't be found.
std::string findAsset(const std::string& relPath);
