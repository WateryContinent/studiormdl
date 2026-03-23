#pragma once
// Precompiled-header substitute for R5-AnimConv source files embedded in studiormdl.
// Uses relative includes instead of the original angle-bracket form so that
// no additional include directories are required in the project.

#include <cstdarg>
#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <regex>
#include <execution>
#include <future>
#include <mutex>
#include <immintrin.h>  // SSE / SVML intrinsics used by math_helper.cpp

// Suppress possible M_PI macro redefinition if _USE_MATH_DEFINES is active
#ifdef M_PI
#undef M_PI
#endif

#include "define.h"
#include "mdl/studio.h"
#include "utils/print.h"
