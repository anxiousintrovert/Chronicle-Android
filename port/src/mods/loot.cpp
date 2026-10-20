#include "loot.hpp"

#include <algorithm>
#include <map>

#include "itemdata.hpp"
#include "monstorunit.hpp"

namespace mods {
namespace {
using Json = nlohmann::json;
using Key = std::pair<int, bool>;

struct Claim {
    std::string owner;
    s16         original{}, applied{};
};

std::map<Key, Claim> claims;

s16 &Value(Key key) {
    auto &model = MonstorTable[key.first];
    return key.second ? model.steal_item : model.rare_item;
}

bool ValidItem(int item) {
    if (item == -1) {
        return true;
    }
    if (item == ITEM_ATTACH_SYNTHESIS_SPHERE) {
        return false;
    }
    if (item >= ITEM_WEAPON_START && item < ITEM_WEAPON_END) {
        return GetWeaponData(item) != nullptr;
    }
    if (item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START) {
        return GetItemData(item) != nullptr;
    }
    return item >= ITEM_ATTACH_START && item <= 130 && GetAttachData(item) != nullptr;
}
} // namespace

bool IsLootWrite(std::string_view op) { return op == "dungeon.loot_set" || op == "dungeon.loot_reset"; }

std::string ValidateLoot(std::string_view op, const Json &a) {
    if (op != "dungeon.loot_get" && !IsLootWrite(op)) {
        return "unknown loot operation";
    }
    if (!a.is_object()) {
        return "loot arguments must be an object";
    }
    if (!a.contains("monster") || !a["monster"].is_number_integer() || a["monster"].get<double>() < 0 || a["monster"].get<double>() > 166) {
        return "monster must be a retail definition ID in 0..166";
    }
    for (const auto &[key, value] : a.items()) {
        if (key == "monster") {
            continue;
        }
        if (op == "dungeon.loot_reset" && key == "field" && value.is_string() && (value == "rare_item" || value == "steal_item")) {
            continue;
        }
        if (op != "dungeon.loot_set" || (key != "rare_item" && key != "steal_item")) {
            return "invalid loot argument: " + key;
        }
        if (!value.is_number_integer() || value.get<double>() < -1 || value.get<double>() >= static_cast<int>(ITEM_WEAPON_END) || !ValidItem(value.get<int>())) {
            return "loot item must be -1 or a valid retail item without a synthesis payload";
        }
    }
    if (op == "dungeon.loot_set" && !a.contains("rare_item") && !a.contains("steal_item")) {
        return "loot_set requires rare_item or steal_item";
    }
    if (op == "dungeon.loot_reset" && !a.contains("field")) {
        return "loot_reset requires field";
    }
    return {};
}

Json ReadLoot(const Json &a) {
    const auto error = ValidateLoot("dungeon.loot_get", a);
    if (!error.empty()) {
        throw std::runtime_error(error);
    }
    const int   id = a.at("monster");
    const auto &model = MonstorTable[id];
    Json        owners = Json::object();
    for (bool steal : {false, true}) {
        const auto it = claims.find({id, steal});
        owners[steal ? "steal_item" : "rare_item"] = it == claims.end() ? "" : it->second.owner;
    }
    return {
        {"monster",         id                    },
        {"rare_item",       model.rare_item       },
        {"steal_item",      model.steal_item      },
        {"attachment_kind", model.attachment_kind },
        {"drops_items",     model.drops_items != 0},
        {"owners",          owners                },
        {"scope",           "future_model_loads"  }
    };
}

bool HasOwnedLoot() { return !claims.empty(); }

void RetainOwnedLoot(std::span<const std::string> owners) {
    for (auto it = claims.begin(); it != claims.end();) {
        if (std::ranges::find(owners, it->second.owner) != owners.end()) {
            ++it;
            continue;
        }
        if (Value(it->first) == it->second.applied) {
            Value(it->first) = it->second.original;
        }
        it = claims.erase(it);
    }
}

struct LootTransaction::Impl {
    std::map<Key, Claim> owners = claims;
    std::map<Key, s16>   values;
};

LootTransaction::LootTransaction() = default;
LootTransaction::~LootTransaction() = default;

bool LootTransaction::Stage(const std::vector<GameCommand> &commands, bool writable, std::string &error) {
    staged_.reset();
    try {
        if (commands.size() > 64) {
            throw std::runtime_error("callback write budget exceeded");
        }
        for (const auto &command : commands) {
            if (!IsLootWrite(command.operation)) {
                continue;
            }
            if (!writable) {
                throw std::runtime_error("loot writes require stable play outside menus and transitions");
            }
            error = ValidateLoot(command.operation, command.arguments);
            if (!error.empty()) {
                throw std::runtime_error(error);
            }
            if (command.owner.empty()) {
                throw std::runtime_error("loot writes require a runtime package owner");
            }
            if (!staged_) {
                staged_ = std::make_unique<Impl>();
            }
            const auto &a = command.arguments;
            const bool  reset = command.operation == "dungeon.loot_reset";
            for (bool steal : {false, true}) {
                const std::string field = steal ? "steal_item" : "rare_item";
                if (reset ? a.at("field") != field : !a.contains(field)) {
                    continue;
                }
                const Key key{a.at("monster").get<int>(), steal};
                auto      found = staged_->owners.find(key);
                if (found != staged_->owners.end() && found->second.owner != command.owner) {
                    throw std::runtime_error("monster " + std::to_string(key.first) + " " + field + " is already owned by " + found->second.owner);
                }
                auto [value, inserted] = staged_->values.try_emplace(key, Value(key));
                if (reset) {
                    if (found != staged_->owners.end()) {
                        if (value->second == found->second.applied) {
                            value->second = found->second.original;
                        }
                        staged_->owners.erase(found);
                    }
                } else {
                    auto &claim = staged_->owners[key];
                    if (claim.owner.empty() || claim.applied != value->second) {
                        claim.original = value->second;
                    }
                    claim.owner = command.owner;
                    claim.applied = a.at(field).get<s16>();
                    value->second = claim.applied;
                }
            }
        }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        staged_.reset();
        return false;
    }
}

void LootTransaction::Commit() noexcept {
    if (!staged_) {
        return;
    }
    for (const auto &[key, value] : staged_->values) {
        Value(key) = value;
    }
    claims.swap(staged_->owners);
    staged_.reset();
}
} // namespace mods
