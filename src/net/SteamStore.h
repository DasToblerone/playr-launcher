#pragma once

#include <string>

#include "net/WorkQueue.h"

namespace playr {

struct OwnedResult {
    std::wstring appId, name;
    bool isGame = false;
};

extern WorkQueue g_coverQueue;

// Downloads missing covers and asks the store about apps that are not classified or owned-but-uninstalled yet.
void QueueBackgroundWork();

}  // namespace playr
