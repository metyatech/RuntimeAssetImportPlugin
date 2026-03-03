#pragma once

// MSVC does not define __has_feature (it is a Clang extension).
// Define it as a no-op macro so headers that use it (e.g. assimp) compile
// without error under MSVC / Unreal Build Tool.
#ifndef __has_feature
#define __has_feature(x) 0
#endif
