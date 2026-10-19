#include "retail_inventory.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>

#include "battlemenu.hpp"
#include "menu_inventory.hpp"
#include "menu_misc.hpp"
#include "userstatus.hpp"
#include "weapon_buildup.hpp"
#include "weaponlevelup.hpp"

namespace mods::retail {
namespace {
using Json = nlohmann::json;

template <class T, std::size_t N>
Json Numbers(const T (&values)[N]) {
    Json result = Json::array();
    for (const auto value : values) {
        result.push_back(static_cast<int>(value));
    }
    return result;
}

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
    const int   character = Character(s, a);
    const auto &weapon = s.chara_weapons[character][a.at("slot").get<int>()];
    if (Definition(weapon.item_no).owner != character) {
        throw std::runtime_error("weapon belongs to a different character");
    }
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
        {"socket",        socket               },
        {"item",          a.item_no            },
        {"sphere_weapon", a.sphere_weapon_no   },
        {"sphere_flags",  a.sphere_flags       },
        {"sphere_level",  a.sphere_level       },
        {"stats",         Numbers(a.status)    },
        {"elements",      Numbers(a.elem)      },
        {"monster_stats", Numbers(a.vs_monster)}
    };
}

ATTACH_LIST BagAttachment(const CUserStatus &s, int slot) {
    ATTACH_LIST result{};
    if (s.consumable_items[slot].id < ITEM_ATTACH_START) {
        result.item_no = s.consumable_items[slot].id;
        return result;
    }
    static_assert(sizeof(result) == sizeof(s.consumable_items[slot]));
    // Native storage is exposed as DNG_CONSUMABLE; avoid aliasing it as another type.
    std::memcpy(&result, &s.consumable_items[slot], sizeof(result));
    return result;
}

void StoreAttachment(CUserStatus &s, int slot, const ATTACH_LIST &attachment) {
    std::memcpy(&s.consumable_items[slot], &attachment, sizeof(attachment));
}

ATTACH_LIST AttachmentDefinition(int item) {
    // The retail table has 50 entries (81-130); metadata for 131 points one past it.
    if (item < ITEM_ATTACH_START || item > 130 || !GetAttachData(item)) {
        throw std::runtime_error("unknown retail attachment");
    }
    ATTACH_LIST result{};
    AttachDataListToHaveCopy(item, &result);
    return result;
}

int AttachmentSpace(const CUserStatus &s) {
    for (int i = 40; i < 43; ++i) {
        if (s.consumable_items[i].id >= ITEM_ATTACH_START) {
            throw std::runtime_error("attachment bag has unresolved overflow");
        }
    }
    for (int i = 0; i < 40; ++i) {
        if (s.consumable_items[i].id < ITEM_ATTACH_START) {
            return i;
        }
    }
    throw std::runtime_error("attachment bag is full");
}

Json BuildUps(const CUserStatus &s, const Json &a, std::optional<int> mardan_fish) {
    auto        weapon = Owned(s, a);
    const auto &source = Definition(weapon.item_no);
    Json        result = Json::array();
    const bool  no_attachments = GetNowWeaponAttachNum(&weapon) == 0;
    if (weapon.item_no == ITEM_WEAPON_MARDAN_EINS || weapon.item_no == ITEM_WEAPON_MARDAN_TWEI) {
        if (!mardan_fish) {
            throw std::runtime_error("Mardan build-up requires saved fishing progress");
        }
        const int needed = weapon.item_no == ITEM_WEAPON_MARDAN_EINS ? 5 : 15;
        result.push_back({
            {"item",               weapon.item_no + 1                      },
            {"fish_required",      needed                                  },
            {"fish_caught",        *mardan_fish                            },
            {"meets_requirements", *mardan_fish >= needed                  },
            {"attachments_empty",  no_attachments                          },
            {"can_build",          *mardan_fish >= needed && no_attachments}
        });
        return result;
    }
    const int base = GetDefaultWeaponNo(source.owner);
    for (int position = 2; position <= MenuCharaWeaponMax[source.owner]; ++position) {
        const int bit = position <= 20 ? position : position - 20;
        if (bit >= 32 || !((position <= 20 ? source.buildup_mask0 : source.buildup_mask1) & (1u << bit))) {
            continue;
        }
        const int   item = base + position;
        const auto &d = Definition(item);
        if (d.owner != source.owner) {
            throw std::runtime_error("invalid retail build-up target owner");
        }
        // Native EnableBuildUpModel initializes destination weapons with global RNG.
        // Only these deterministic fields are used by its requirement comparator.
        WEAPON_HAVE destination{};
        destination.attack = d.attack;
        destination.magic = d.magic;
        for (int i = 0; i < 5; ++i) {
            destination.elem[i] = d.elem[i];
        }
        for (int i = 0; i < 10; ++i) {
            destination.vs_monster[i] = d.vs_monster[i];
        }
        const bool meets = CompareBuildUpModelData2(&destination, &weapon) != 0;
        result.push_back({
            {"item",               item                                                                                                                                                       },
            {"meets_requirements", meets                                                                                                                                                      },
            {"attachments_empty",  no_attachments                                                                                                                                             },
            {"can_build",          meets && no_attachments                                                                                                                                    },
            {"requirements",       {{"attack", destination.attack}, {"magic", destination.magic}, {"elements", Numbers(destination.elem)}, {"monster_stats", Numbers(destination.vs_monster)}}}
        });
    }
    return result;
}

void Synthesize(CUserStatus &s, const Json &a) {
    auto &w = s.chara_weapons[Character(s, a)][a.at("slot").get<int>()];
    Owned(s, a);
    if (s.equipped_weapon_slot[Character(s, a)] == a.at("slot").get<int>()) {
        throw std::runtime_error("cannot status-break an equipped weapon");
    }
    if (!WeaponStatusBreakEnable(&w)) {
        throw std::runtime_error("status-break requires weapon level 5 or higher");
    }
    const int   slot = AttachmentSpace(s);
    ATTACH_LIST sphere{}, total{};
    sphere.item_no = ITEM_ATTACH_SYNTHESIS_SPHERE;
    sphere.sphere_weapon_no = total.sphere_weapon_no = w.item_no;
    sphere.sphere_level = w.level;
    sphere.status[0] = 0.6f * w.attack;
    sphere.status[1] = 0.6f * w.endurance;
    sphere.status[2] = 0.6f * w.speed;
    sphere.status[3] = 0.6f * w.magic;
    for (int i = 0; i < 5; ++i) {
        sphere.elem[i] = 0.6f * w.elem[i];
    }
    for (int i = 0; i < 10; ++i) {
        sphere.vs_monster[i] = 0.6f * w.vs_monster[i];
    }
    int flags = w.flags;
    // The native status-break calculator caps fitted values first, then applies
    // 60 percent separately to base and fitted values. It consumes the sockets.
    for (int i = 0; i < 6; ++i) {
        auto &attachment = w.attach[i];
        if (attachment.item_no < ITEM_ATTACH_START) {
            continue;
        }
        AttachmentDefinition(attachment.item_no);
        AttachMentValuePlus(&total, &attachment, w.attach_kind[i] == 3 ? 2.0f : 1.0f);
        if (attachment.item_no == ITEM_ATTACH_SYNTHESIS_SPHERE && attachment.sphere_flags != 0 && attachment.sphere_flags != 1) {
            flags |= attachment.sphere_flags;
        }
    }
    AttachMentValuePlus(&sphere, &total, 0.6f);
    sphere.sphere_flags = CheckWeaponOptionStatus(flags);
    StoreAttachment(s, slot, sphere);
    if (IsDefaultWeapon(w.item_no) >= 0) {
        // Default weapons reset instead of disappearing; this native branch uses no RNG.
        WepDataListToHaveCopy(w.item_no, &w);
    } else {
        w = {};
        w.item_no = -1;
    }
}
} // namespace

bool Handles(std::string_view op) {
    return op == "inventory.count" || op == "inventory.give" || op == "inventory.remove" ||
           op == "weapons.give" || op == "weapons.abs_get" || op == "weapons.abs_add" ||
           op == "weapons.attachments_get" || op == "items.native_definition" ||
           op == "attachments.list" || op == "attachments.give" || op == "attachments.remove" ||
           op == "weapons.build_up_list" || op == "weapons.synthesize";
}

std::string Validate(std::string_view op, const Json &a) {
    if (!a.is_object()) {
        return "arguments must be an object";
    }
    std::map<std::string, std::pair<double, double>> fields;
    const bool                                       item = op.starts_with("inventory.") || op == "weapons.give" || op == "items.native_definition";
    if (op == "attachments.give") {
        fields["item"] = {81, 130};
        fields["quantity"] = {1, 40};
        fields["level"] = {1, 3};
    } else if (op == "attachments.remove") {
        fields["bag_slot"] = {0, 42};
    } else if (op == "attachments.list") {
        // Empty object only.
    } else if (item) {
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
        if (key != "character" && key != "synthesis" && key != "level" && !a.contains(key)) {
            return "missing retail argument: " + key;
        }
    }
    if (op == "attachments.give" && a.contains("level") && (a.at("item").get<int>() < ITEM_ATTACH_ATTACK || a.at("item").get<int>() > ITEM_ATTACH_MAGICAL_POWER)) {
        return "level is only valid for retail stat attachments 91-94";
    }
    return {};
}

Json Read(const CUserStatus &s, std::string_view op, const Json &a, std::optional<int> mardan_fish) {
    if (op == "weapons.build_up_list") {
        return BuildUps(s, a, mardan_fish);
    }
    if (op == "attachments.list") {
        Json result = Json::array();
        for (int i = 0; i < 43; ++i) {
            auto record = Attachment(BagAttachment(s, i), i);
            record.erase("socket");
            record["bag_slot"] = i;
            record["overflow"] = i >= 40;
            result.push_back(std::move(record));
        }
        return {
            {"capacity", 40    },
            {"slots",    result}
        };
    }
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
                {"item",          item                 },
                {"kind",          "weapon"             },
                {"owner",         d.owner              },
                {"attack",        d.attack             },
                {"endurance",     d.endurance          },
                {"speed",         d.speed              },
                {"magic",         d.magic              },
                {"durability",    d.durability         },
                {"socket_kinds",  Numbers(d.hole)      },
                {"socket_count",  d.hole_num           },
                {"elements",      Numbers(d.elem)      },
                {"monster_stats", Numbers(d.vs_monster)},
                {"attack_max",    d.attack_max         },
                {"magic_max",     d.magic_max          }
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
        auto result = Attachment(AttachmentDefinition(item), -1);
        result.erase("socket");
        result["kind"] = "attachment";
        return result;
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
    if (op == "weapons.synthesize") {
        Synthesize(s, a);
        return;
    }
    if (op == "attachments.give") {
        const int item = a.at("item");
        if (item == ITEM_ATTACH_SYNTHESIS_SPHERE) {
            throw std::runtime_error("SynthSpheres must come from owned weapons via weapons.synthesize");
        }
        auto attachment = AttachmentDefinition(item);
        if (item >= ITEM_ATTACH_ATTACK && item <= ITEM_ATTACH_MAGICAL_POWER) {
            attachment.status[item - ITEM_ATTACH_ATTACK] += a.value("level", 1);
        }
        for (int i = 0; i < a.at("quantity").get<int>(); ++i) {
            StoreAttachment(s, AttachmentSpace(s), attachment);
        }
        return;
    }
    if (op == "attachments.remove") {
        const int slot = a.at("bag_slot");
        if (s.consumable_items[slot].id < ITEM_ATTACH_START) {
            throw std::runtime_error("attachment bag slot is empty");
        }
        AttachmentDefinition(s.consumable_items[slot].id);
        ATTACH_LIST empty{};
        empty.item_no = -1;
        StoreAttachment(s, slot, empty);
        return;
    }
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
