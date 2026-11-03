#pragma once

#include "platform/Win32.h"

#include <algorithm>
using std::min;  // gdiplus.h uses unqualified min and max, which NOMINMAX removed
using std::max;

#include <gdiplus.h>
