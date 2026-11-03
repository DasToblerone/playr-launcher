#pragma once

#include "platform/Win32.h"

namespace playr {

// The search field and the in-place rename field are child EDIT controls drawn over the custom UI.
void CreateEditControls(HWND parent);
void DestroyEditControls();
void ApplySearch(HWND hwnd);

}  // namespace playr
