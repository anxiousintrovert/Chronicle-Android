#pragma once
#include "runtime.hpp"
class CSaveData;
class CDungeonMap;
namespace mods {
std::string ValidateAtla(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadAtlaSnapshot(CSaveData &save, const CDungeonMap &map, int dungeon, int floor, bool back);
bool CollectAtla(CSaveData &save, CDungeonMap &map, int dungeon, int floor, int slot, std::string &error);
nlohmann::json ReadAtla();
bool ApplyAtla(const std::vector<GameCommand> &commands, int money, std::string &error);
}
