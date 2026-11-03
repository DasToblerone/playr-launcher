#pragma once

#include "model/Game.h"
#include "platform/Win32.h"

namespace playr {

void SelectSort(HWND hwnd, SortKey key);

// Leaves rename mode. Focus only goes back to the window when the edit did not lose it to another control.
void EndRename(HWND hwnd, bool restoreFocus);
void StartRename(HWND hwnd, int chip);
void CommitRename(HWND hwnd);

// "+ Erstellen" in the sidebar: adds a category with a default name and lets you type the real one right away.
void CreateCategoryRow(HWND hwnd);
void DeleteCategory(HWND hwnd, int index);

void EndDrag(HWND hwnd);
void DropOnCategory(HWND hwnd);

}  // namespace playr
