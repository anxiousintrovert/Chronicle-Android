#include "retail_inventory.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

#include "battlemenu.hpp"
#include "menu_inventory.hpp"
#include "userstatus.hpp"

namespace mods::retail {
namespace {
using Json = nlohmann::json;

int Character(const CUserStatus &s, const Json &a) {
    int id = a.value("character", static_cast<int>(s.cur_chara));
    if (id < 0 || id >= std::clamp(static_cast<int>(s.party_size), 0, 6)) {
        throw std::runtime_error("character is not joined");
    }
    return id;
}

const WEAPON_DATA &Definition(int item) {
    if (item < ITEM_WEAPON_START || item >= ITEM_WEAPON_END) {
        throw std::runtime_error("not a retail weapon ID");
    }
    const auto *data = GetWeaponData(item);
    if (!data || data->owner >= 6 || data->hole_num > 6) {
        throw std::runtime_error("invalid retail weapon definition");
    }
    return *data;
}

const WEAPON_HAVE &Owned(const CUserStatus &s, const Json &a) {
    const auto &weapon = s.chara_weapons[Character(s, a)][a.at("slot").get<int>()];
    Definition(weapon.item_no);
    if (weapon.level < 0 || weapon.level > 99) {
        throw std::runtime_error("invalid native weapon level");
    }
    return weapon;
}

int Held(const CUserStatus &s, int item = -1) {
    int count = 0;
    for (int i = 0; i < 103; ++i) {
        if (s.item_pack.item[i] >= ITEM_DUNGEON_START && (item < 0 || s.item_pack.item[i] == item)) {
            ++count;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (s.item_pack.quick_item_slot[i] < ITEM_DUNGEON_START) {
            continue;
        }
        int n = s.item_pack.quick_item_qty[i];
        if (n < 0 || n > 9) {
            throw std::runtime_error("invalid native quick-slot quantity");
        }
        if (item < 0 || s.item_pack.quick_item_slot[i] == item) {
            count += n;
        }
    }
    if (count > 103) {
        throw std::runtime_error("invalid native inventory count");
    }
    return count;
}

uint32_t Next(uint32_t &seed) {
    // Defined modulo 2^32; never consumes the retail/global random stream.
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

Json Attachment(const ATTACH_LIST &a, int socket) {
    return {
        {"socket",        socket            },
        {"item",          a.item_no         },
        {"sphere_weapon", a.sphere_weapon_no},
        {"sphere_flags",  a.sphere_flags    },
        {"sphere_level",  a.sphere_level    },
        {"stats",         a.status          },
        {"elements",      a.elem            },
        {"monster_stats", a.vs_monster      }
    };
}
} // namespace

bool Handles(std::string_view op) {
    return op == "inventory.count" || op == "inventory.give" || op == "inventory.remove" ||
           op == "weapons.give" || op == "weapons.abs_get" || op == "weapons.abs_add" ||
           op == "weapons.attachments_get" || op == "items.native_definition";
}

std::string Validate(std::string_view op, const Json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    std::map<std::string, std::pair<double, double>> fields;
    const bool                                       item = op.starts_with("inventory.") || op == "weapons.give" || op == "items.native_definition";
    if (item) {
        fields["item"] = op == "weapons.give" ? std::pair<double, double>{257, 376} : op == "items.native_definition" ? std::pair<double, double>{81, 376}
                                                                                                                      : std::pair<double, double>{132, 256};
    } else {
        fields["character"] = {0, 5};
        fields["slot"] = {0, 10};
    }
    if (op == "inventory.give" || op == "inventory.remove") {
        fields["quantity"] = {1, 100};
    }
    if (op == "inventory.give" || op == "weapons.give") {
        fields["seed"] = {0, 4294967295.0};
    }
    if (op == "weapons.abs_add") {
        fields["amount"] = {0, 32767};
        fields["synthesis"] = {0, 32767};
    }
    for (const auto &[key, value] : a.items()) {
        auto field = fields.find(key);
        if (field == fields.end()) {
            return "unknown retail argument: " + key;
        }
        if (!value.is_number_integer() || value.get<double>() < field->second.first || value.get<double>() > field->second.second) {
            return "invalid retail argument: " + key;
        }
    }
    for (const auto &[key, range] : fields) {
        if (key != "character" && key != "synthesis" && !a.contains(key)) {
            return "missing retail argument: " + key;
        }
    }
    return {};
}

Json Read(const CUserStatus &s, std::string_view op, const Json &a) {
    if (op == "inventory.count") {
        const int item = a.at("item");
        return {
            {"item", item},
            {"quantity", item == ITEM_WORLDMAP ? (s.special_flag_238 ? 1 : 0) : Held(s, item)}
        };
    }
    if (op == "items.native_definition") {
        const int item = a.at("item");
        if (item >= ITEM_WEAPON_START) {
            const auto &d = Definition(item);
            return {
                {"item",          item        },
                {"kind",          "weapon"    },
                {"owner",         d.owner     },
                {"attack",        d.attack    },
                {"endurance",     d.endurance },
                {"speed",         d.speed     },
                {"magic",         d.magic     },
                {"durability",    d.durability},
                {"socket_kinds",  d.hole      },
                {"socket_count",  d.hole_num  },
                {"elements",      d.elem      },
                {"monster_stats", d.vs_monster},
                {"attack_max",    d.attack_max},
                {"magic_max",     d.magic_max }
            };
        }
        if (item >= ITEM_DUNGEON_START) {
            const auto *d = GetItemData(item);
            if (!d) {
                throw std::runtime_error("unknown retail item");
            }
            return {
                {"item",       item         },
                {"kind",       "item"       },
                {"volume",     d->vol       },
                {"use_flags",  d->use_flags },
                {"kind_flags", d->kind_flags},
                {"stack_kind", d->stack_kind}
            };
        }
        const auto *d = GetAttachData(item);
        if (!d) {
            throw std::runtime_error("unknown retail attachment");
        }
        return {
            {"item", item        },
            {"kind", "attachment"}
        };
    }
    const auto &w = Owned(s, a);
    if (op == "weapons.abs_get") {
        auto copy = w; // Native signature is mutable; never hand it a live object.
        return {
            {"abs",       w.experience          },
            {"threshold", GetWeaponMaxExp(&copy)},
            {"synthesis", w.synthesis_count     },
            {"level",     w.level               }
        };
    }
    if (op == "weapons.attachments_get") {
        Json        result = Json::array();
        const auto &d = Definition(w.item_no);
        for (int i = 0; i < d.hole_num; ++i) {
            auto record = Attachment(w.attach[i], i);
            record["kind"] = w.attach_kind[i];
            record["definition_kind"] = d.hole[i];
            result.push_back(std::move(record));
        }
        return result;
    }
    throw std::runtime_error("retail operation is not a query");
}

void Apply(CUserStatus &s, std::string_view op, const Json &a) {
    if (op == "weapons.abs_add") {
        const auto &checked = Owned(s, a);
        auto       &w = s.chara_weapons[Character(s, a)][a.at("slot").get<int>()];
        const auto  sum = static_cast<int64_t>(checked.synthesis_count) + a.value("synthesis", 0);
        if (sum < 0 || sum > std::numeric_limits<int32_t>::max()) {
            throw std::runtime_error("synthesis counter overflow");
        }
        w.experience = std::clamp(w.experience + a.at("amount").get<int>(), 0, GetWeaponMaxExp(&w));
        w.synthesis_count = static_cast<int32_t>(sum);
        return;
    }
    const int item = a.at("item");
    if (op == "weapons.give") {
        const auto &d = Definition(item);
        if (d.owner >= std::clamp(static_cast<int>(s.party_size), 0, 6)) {
            throw std::runtime_error("weapon owner is not joined");
        }
        auto &slots = s.chara_weapons[d.owner];
        int   slot = 0;
        while (slot < 10 && slots[slot].item_no >= ITEM_WEAPON_START) {
            ++slot;
        }
        if (slot == 10 || slots[10].item_no >= ITEM_WEAPON_START) {
            throw std::runtime_error("weapon inventory full or overflowing");
        }
        WEAPON_HAVE w{};
        w.item_no = item;
        w.attack = d.attack;
        w.endurance = d.endurance;
        w.speed = d.speed;
        w.magic = d.magic;
        w.durability = d.durability;
        w.durability_f = d.durability;
        w.flags = d.flags;
        w.best_elem = 5;
        int best = 0;
        for (int i = 0; i < 5; ++i) {
            w.elem[i] = d.elem[i];
            if (w.elem[i] > best) {
                best = w.elem[i];
                w.best_elem = i;
            }
        }
        for (int i = 0; i < 10; ++i) {
            w.vs_monster[i] = d.vs_monster[i];
        }
        uint32_t seed = a.at("seed").get<uint32_t>();
        if (IsDefaultWeapon(item) < 0) {
            auto chance = Next(seed) % 1000;
            if (chance < 10) {
                w.flags |= WEAPONFLAG_DURABLE;
            }
            if (chance > 989) {
                w.flags |= WEAPONFLAG_FRAGILE;
            }
        }
        slots[slot] = w;
        return;
    }
    const auto *d = GetItemData(item);
    if (!d) {
        throw std::runtime_error("unknown retail item");
    }
    int quantity = a.at("quantity");
    if (item == ITEM_WORLDMAP) {
        if (quantity != 1 || (op == "inventory.remove" && !s.special_flag_238)) {
            throw std::runtime_error("world map quantity must be one and removal requires ownership");
        }
        s.special_flag_238 = op == "inventory.give";
        return;
    }
    if (op == "inventory.give") {
        int capacity = s.item_pack.num, count = Held(s);
        if (capacity < 0 || capacity > 100 || quantity > capacity - count) {
            throw std::runtime_error("item inventory full or overflowing");
        }
        uint32_t seed = a.at("seed").get<uint32_t>();
        for (int i = 0; i < capacity && quantity > 0; ++i) {
            if (s.item_pack.item[i] >= ITEM_DUNGEON_START) {
                continue;
            }
            int volume = d->vol;
            if (item == ITEM_DRAN_S_FEATHER) {
                int angle = Next(seed) % 360;
                if (angle < 180) {
                    angle += 180;
                }
                volume = angle * 60;
            }
            if (item >= ITEM_ANTI_FREEZE_AMULET && item <= ITEM_ANTIDOTE_AMULET) {
                volume = Next(seed) % 5 + 3;
            }
            s.item_pack.item[i] = item;
            s.item_pack.item_vol[i] = volume;
            --quantity;
        }
        if (quantity) {
            throw std::runtime_error("no free native item slots");
        }
    } else if (op == "inventory.remove") {
        if (Held(s, item) < quantity) {
            throw std::runtime_error("insufficient owned items");
        }
        for (int i = 0; i < 103 && quantity > 0; ++i) {
            if (s.item_pack.item[i] == item) {
                s.item_pack.item[i] = -1;
                s.item_pack.item_vol[i] = 0;
                --quantity;
            }
        }
        for (int i = 0; i < 3 && quantity > 0; ++i) {
            if (s.item_pack.quick_item_slot[i] == item) {
                int take = std::min(quantity, static_cast<int>(s.item_pack.quick_item_qty[i]));
                s.item_pack.quick_item_qty[i] -= take;
                quantity -= take;
                if (!s.item_pack.quick_item_qty[i]) {
                    s.item_pack.quick_item_slot[i] = -1;
                }
            }
        }
    } else {
        throw std::runtime_error("retail operation is not a write");
    }
    s.item_pack.item_count = Held(s);
}
} // namespace mods::retail
