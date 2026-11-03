#include "steam/Steam.h"

#include <algorithm>

#include "model/Library.h"
#include "util/Text.h"
#include "util/Vdf.h"

namespace playr {

// Steam keeps collections in userdata/<account>/config/cloudstorage. The hidden list is skipped.
std::vector<SteamCollection> ReadSteamCollections() {
    std::vector<SteamCollection> collections;
    std::error_code ec;
    std::wstring steam = SteamPath();
    if (!steam.empty()) {
        for (fs::directory_iterator it(fs::path(steam) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
            std::wstring text = ReadTextFile(it->path() / L"config" / L"cloudstorage" / L"cloud-storage-namespace-1.json");
            const std::wstring keyMarker = L"\"key\":\"user-collections.";
            for (size_t pos = 0; (pos = text.find(keyMarker, pos)) != std::wstring::npos;) {
                size_t idPos = pos + keyMarker.size();
                size_t entryEnd = text.find(L"}],[", pos);
                size_t valuePos = text.find(L"\"value\":\"", pos);
                bool hidden = ReadJsonString(text, idPos) == L"hidden";
                pos = idPos;
                // Deleted collections have no value of their own; the next hit belongs to another entry.
                if (hidden || valuePos == std::wstring::npos || valuePos > entryEnd) continue;

                std::wstring inner = ReadJsonString(text, valuePos + 9);
                size_t namePos = inner.find(L"\"name\":\"");
                size_t addedPos = inner.find(L"\"added\":[");
                if (namePos == std::wstring::npos || addedPos == std::wstring::npos) continue;

                SteamCollection collection;
                collection.name = ReadJsonString(inner, namePos + 8);
                size_t listEnd = inner.find(L']', addedPos);
                std::wstring list = inner.substr(addedPos + 9, listEnd - addedPos - 9);
                for (size_t start = 0; start < list.size();) {
                    size_t comma = list.find(L',', start);
                    if (comma == std::wstring::npos) comma = list.size();
                    collection.appIds.push_back(list.substr(start, comma - start));
                    start = comma + 1;
                }
                if (!collection.name.empty() && !collection.appIds.empty()) collections.push_back(collection);
            }
        }
    }
    return collections;
}

// Adds the games of every Steam collection that holds at least one game of the library to the category of the same name.
ImportResult ImportSteamCategories() {
    struct Collection {
        std::wstring name;
        std::vector<Game*> members;
    };
    std::vector<Collection> collections;
    for (const auto& raw : ReadSteamCollections()) {
        Collection collection;
        collection.name = raw.name;
        for (const auto& id : raw.appIds)
            for (auto& game : g_games)
                if (game.appId == id) collection.members.push_back(&game);
        if (!collection.members.empty()) collections.push_back(collection);
    }

    ImportResult result;
    for (const auto& collection : collections) {
        const std::wstring& name = collection.name;
        if (IsReservedCategory(name) || name.find_first_of(L"\t\r\n") != std::wstring::npos) continue;
        int added = (int)std::count_if(collection.members.begin(), collection.members.end(),
            [&](const Game* game) { return !game->InCategory(name); });
        bool isNew = std::find(g_categories.begin(), g_categories.end(), name) == g_categories.end();
        if (added == 0 && !isNew) continue;
        if (isNew) {
            g_categories.push_back(name);
            ++result.categories;
        }
        for (Game* game : collection.members) game->AddCategory(name);
        result.games += added;
    }
    return result;
}

}  // namespace playr
