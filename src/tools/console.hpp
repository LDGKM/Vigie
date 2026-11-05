#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // Sinon windows.h remplace std::min et std::max par ses propres macros.
#endif
#include <windows.h>
#endif

namespace vigie::tools {

// Affiche correctement les accents dans la console Windows.
inline void use_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

}  // namespace vigie::tools
