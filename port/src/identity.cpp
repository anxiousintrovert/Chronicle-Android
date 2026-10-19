#include "identity.hpp"
#include "icon_retail.hpp"

#include "icon/chronicle.hpp"
#include "platform/game_icon.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC");
    PathsConfigure({.app_dir = "chronicle", .env_data = "DC_DATA", .env_save = "DC_SAVE"});
    WindowSetIdentity({.title = "Dark Cloud",
                       .app_id = "dcdecomp.darkcloud",
                       .icon_rgba = kIconRgba,
                       .icon_size = kIconSize});
}

void GameSetDataIcon() {
    // The save menu's pack in each language folder (menu_draw.cpp's ComMenuContryName) carries the memory
    // card icon; a disc holds only its own languages, so every folder is tried, English first.
    const auto source = RetailIconSource(PathsDataRoot());
    GameIconInstall(PathsDataRoot(), source);
}
