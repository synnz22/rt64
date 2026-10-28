//
// RT64
//

#pragma once

#include <filesystem>

#include "rt64_timer.h"

namespace RT64 {
    struct DynamicLibraries {
        static bool load();

        // Directory of the module this code is linked into (the executable when RT64 is a static library).
        static bool moduleDirectory(std::filesystem::path &path);
    };
};