#pragma once
#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "owned_enemy_scale.hpp"
#include "owned_enemy_stats.hpp"
class CMonstorUnit;
class CSaveData;
class CUserStatus;

namespace mods {
struct GameCommand;
struct GeoramaState;
struct CommitEffects;
struct EquipmentContext;
bool        ApplyEntityAndSave(CSaveData &save, CMonstorUnit *unit, const std::vector<GameCommand> &commands,
                               int money, std::string &error, CommitEffects *effects, GeoramaState *georama, const EquipmentContext *equipment = nullptr, bool shops_writable = false, bool town_loadout = false);
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
        bool                 touched = false;
        EnemyStatValues      stats{};
        int                  slow = 0, stop = 0;
        int                  reaction = 0, damage = 0;
        bool                 scale_touched = false;
        EnemyScale           scale{};
        bool                 velocity_touched = false;
        std::array<float, 3> direction{};
        float                speed = 0;
    };

    CMonstorUnit                   *unit_ = nullptr;
    std::array<Values, 16>          values_{};
    std::optional<EnemyStatOwners>  owners_;
    std::optional<EnemyScaleOwners> scales_;
};

std::string    ValidateEntityQuery(std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadEntities(CMonstorUnit *unit, std::string_view operation, const nlohmann::json &arguments);
nlohmann::json ReadCombat(CMonstorUnit *unit, const CUserStatus &status);
} // namespace mods
