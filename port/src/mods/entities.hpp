#pragma once
#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>
class CMonstorUnit;
class CSaveData;

namespace mods {
struct GameCommand;
struct GeoramaState;
struct CommitEffects;
bool        ApplyEntityAndSave(CSaveData &save, CMonstorUnit *unit, const std::vector<GameCommand> &commands,
                               int money, std::string &error, CommitEffects *effects, GeoramaState *georama);
bool        IsEntityWrite(std::string_view operation);
std::string ValidateEntityWrite(std::string_view operation, const nlohmann::json &arguments);

// Staging does not publish native changes. The game bridge commits this only
// after every save/scene command in the same callback has succeeded.
class EntityTransaction {
public:
    bool Stage(CMonstorUnit *unit, const std::vector<GameCommand> &commands, std::string &error);
    void Commit() noexcept;

private:
    struct Values {
        bool touched = false;
        int  hp = 0, slow = 0, stop = 0;
    };

    CMonstorUnit          *unit_ = nullptr;
    std::array<Values, 16> values_{};
};

std::string    ValidateEntityQuery(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadEntities(CMonstorUnit *unit, std::string_view operation, const nlohmann::json &arguments);
} // namespace mods
