#pragma once
// =====================================================================
//  SynthMiner  --  which build is this?
// ---------------------------------------------------------------------
//  CMake writes app_version.h into the build directory on every build
//  (main/app_version.h.in): the git commit, the build time, and the
//  release out of metadata.json. Anything that REPORTS the build reads
//  it through here.
//
//  WHY THIS FILE EXISTS. The include dance and the "unknown" fallback
//  used to be copied into each reporter, and the copy in main.c had the
//  fallback WITHOUT the include -- so every flight recorder file ever
//  written said `build=unknown`, including the ones from the session
//  that was supposed to explain a render bug (F-129). The debug console
//  next to it reported the real hash the whole time, which is why
//  nobody noticed.
//
//  One copy, so there is no second one to forget. "unknown" survives
//  only for a build with no git and no generated header at all.
// =====================================================================

#if defined(__has_include)
#if __has_include("app_version.h")
#include "app_version.h"
#endif
#endif

#ifndef APP_GIT_HASH
#define APP_GIT_HASH "unknown"
#endif
#ifndef APP_BUILD_TIME
#define APP_BUILD_TIME "unknown"
#endif
