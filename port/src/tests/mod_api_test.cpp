#include <gtest/gtest.h>

#include <cstring>

#include "dun/gameloop.hpp"
#include "mods/api.hpp"
#include "mods/georama.hpp"
#include "mods/native.hpp"
#include "savedata.hpp"
#include "shot_freefuncs.hpp"
#include "userstatus.hpp"

namespace {
using Json = nlohmann::json;

TEST(ModApi, DetachedQueriesAndEmptyWritesPreserveEveryNativeByteAndRng) {
    CUserStatus status{};
    status.party_size = 1;
    status.hp[0] = 24;
    status.max_hp[0] = 100;
    status.item_pack.num = 1;
    status.item_pack.item[0] = 81;
    status.chara_weapons[0][0].item_no = ITEM_WEAPON_START;
    status.money = 20;
    const CUserStatus before = status;
    srand(715);
    const int expected = rand();
    srand(715);
    for (const auto &op : mods::Operations()) {
        if (!op.writes && op.name != "render.camera_get" && op.name != "dungeon.layout_get" && op.name != "dungeon.loot_get" && op.name != "dungeon.chest_loot_get" && op.name != "dungeon.floor_rules_get" && op.name != "dungeon.back_floor_state" && op.name != "world.transition_state" && op.name != "items.quick_slots" && op.name != "player.combat_state" && op.name != "player.tuning_state" && op.name != "party.town_state" && op.name != "party.switch_state" && op.name != "weapons.equip_state" && op.name != "weapons.refresh_state" && op.name != "combat.get" && !op.name.starts_with("entities.") && !op.name.starts_with("physics.") && !op.name.starts_with("services.") && !op.name.starts_with("shops.") && !op.name.starts_with("story.") && !op.name.starts_with("map.") && !op.name.starts_with("fishing.") && !op.name.starts_with("georama.")) {
            Json args = Json::object();
            if (op.name.starts_with("weapons.") && op.name != "weapons.list") {
                args["slot"] = 0;
            }
            if (op.name == "inventory.count" || op.name == "items.native_definition") {
                args["item"] = ITEM_BREAD;
            }
            auto snapshot = mods::ReadStatus(status, {1, 0, 20, true}, op.name, args);
            snapshot = nullptr;
        }
    }
    std::string error;
    ASSERT_TRUE(mods::ApplyStatus(status, {}, 0, error));
    EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
    EXPECT_EQ(rand(), expected);
}

TEST(ModApi, BatchValidationRollsBackHealthAndMoneyTogether) {
    CUserStatus status{};
    status.party_size = 1;
    status.max_hp[0] = 100;
    status.hp[0] = 20;
    status.money = 5;
    const CUserStatus before = status;
    std::string       error;
    EXPECT_FALSE(mods::ApplyStatus(status, {
                                               {"player.heal",   {{"amount", 15}}},
                                               {"economy.spend", {{"amount", 10}}}
    },
                                   0, error));
    EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
    EXPECT_EQ(error, "insufficient currency");
    EXPECT_TRUE(mods::ApplyStatus(status, {
                                              {"player.heal",   {{"amount", 150}}},
                                              {"economy.spend", {{"amount", 5}}  }
    },
                                  0, error));
    EXPECT_EQ(status.hp[0], 100);
    EXPECT_EQ(status.next_hp[0], 100);
    EXPECT_EQ(status.money, 0);
}

TEST(ModApi, RejectsInvalidCallsAndUnavailableCharacters) {
    CUserStatus status{};
    status.party_size = 1;
    std::string       error;
    const CUserStatus before = status;
    for (const mods::GameCommand &command : std::vector<mods::GameCommand>{
             {"player.set",        {{"hp", 1.5}}                    },
             {"player.heal",       {{"amount", -1}}                 },
             {"player.heal",       {{"amount", 1}, {"character", 1}}},
             {"player.set",        {{"hp", 1}, {"max_hp", 1}}       },
             {"player.thirst_set", {{"water", 1}, {"restore", 1}}   },
             {"entities.spawn",    Json::object()                   }
    }) {
        EXPECT_FALSE(mods::ApplyStatus(status, {command}, 0, error));
        EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
    }
}

TEST(ModApi, ThirstWritesClampAndCancelOnlyRequestedCharacterInterpolation) {
    CUserStatus status{};
    status.party_size = 2;
    status.water_max[1] = 20;
    status.water_now[1] = 19;
    status.drink_step[0] = 7;
    status.drink_step[1] = 5;
    std::string error;
    ASSERT_TRUE(mods::ApplyStatus(status, {
                                              {"player.thirst_set", {{"character", 1}, {"restore", 10}}}
    },
                                  0, error));
    EXPECT_EQ(status.water_now[1], 20);
    EXPECT_EQ(status.drink_step[1], 0);
    EXPECT_EQ(status.drink_step[0], 7);
}

TEST(ModApi, SaveTransactionsPreserveOrderAndRollbackAcrossDomains) {
    auto save = std::make_unique<CSaveData>();
    save->SetGameFlag(8191, 0);
    save->SetFishingPoint(10);
    save->GetDngStatus()->money = 3;
    std::string error;
    EXPECT_FALSE(mods::ApplySave(*save, {
                                            {"story.flag_set", {{"id", 8191}, {"value", 1}}},
                                            {"fishing.spend",  {{"amount", 11}}            }
    },
                                 2, error));
    EXPECT_EQ(save->GetGameFlag(8191), 0);
    EXPECT_EQ(save->GetFishingPoint(), 10);
    EXPECT_EQ(save->GetDngStatus()->money, 3);
    ASSERT_TRUE(mods::ApplySave(*save, {
                                           {"story.int_set",     {{"id", 31}, {"value", -2147483648LL}}  },
                                           {"map.flag_set",      {{"map", 79}, {"id", 511}, {"value", 1}}},
                                           {"map.init_flag_set", {{"map", 79}, {"id", 511}, {"value", 1}}},
                                           {"fishing.add",       {{"amount", 20}}                        },
                                           {"fishing.spend",     {{"amount", 30}}                        }
    },
                                0, error));
    EXPECT_EQ(save->GetGameIntFlag(31), -2147483648LL);
    EXPECT_EQ(save->GetMapFlag(79, 511), 1);
    EXPECT_EQ(save->GetMapInitFlag(79, 511), 1);
    EXPECT_EQ(save->GetFishingPoint(), 0);
}

TEST(ModApi, SaveReadsAndEmptyCallbacksPreserveBytesAndRandomStream) {
    auto               save = std::make_unique<CSaveData>();
    mods::GeoramaState scene;
    scene.town = 0;
    scene.plots[0].available = true;
    const auto before = std::make_unique<CSaveData>(*save);
    srand(811);
    const auto expected = rand();
    srand(811);
    for (const auto &op : mods::Operations()) {
        if (op.writes || op.name == "render.camera_get" || op.name == "georama.placement_state" || op.name == "georama.atla_list" || op.name == "dungeon.layout_get" || (op.name == "dungeon.loot_get" || op.name == "dungeon.chest_loot_get") || op.name == "dungeon.floor_rules_get" || op.name == "dungeon.back_floor_state" || op.name == "world.transition_state" || op.name == "items.quick_slots" || op.name == "player.combat_state" || op.name == "player.tuning_state" || op.name == "party.town_state" || op.name == "party.switch_state" || op.name == "weapons.equip_state" || op.name == "weapons.refresh_state" || op.name == "combat.get" || (op.name.starts_with("entities.") || op.name.starts_with("physics.") || op.name.starts_with("services.") || op.name.starts_with("shops."))) {
            continue;
        }
        Json a = Json::object();
        if (op.name.starts_with("weapons.") && op.name != "weapons.list") {
            a["slot"] = 0;
        }
        if (op.name == "inventory.count" || op.name == "items.native_definition") {
            a["item"] = ITEM_BREAD;
        }
        if (op.name.starts_with("story.") || op.name.starts_with("map.")) {
            a["id"] = 0;
        }
        if (op.name.starts_with("map.")) {
            a["map"] = 0;
        }
        if (op.name == "georama.get" || op.name == "georama.satisfaction_get") {
            a["town"] = 0;
        }
        if (op.name == "georama.elements_get") {
            a = {
                {"town", 0},
                {"plot", 0}
            };
        }
        EXPECT_NO_THROW(mods::ReadSave(*save, {1, 0, 0, true}, op.name, a, &scene));
    }
    std::string error;
    ASSERT_TRUE(mods::ApplySave(*save, {}, 0, error));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), expected);
}

TEST(ModApi, RejectsFlagIndicesBeforeCallingNativeAccessors) {
    for (const mods::GameCommand &c : std::vector<mods::GameCommand>{
             {"story.int_get",     {{"id", -1}}                        },
             {"story.int_get",     {{"id", 32}}                        },
             {"story.flag_get",    {{"id", 8192}}                      },
             {"map.flag_get",      {{"map", 80}, {"id", 0}}            },
             {"map.init_flag_get", {{"map", 0}, {"id", 512}}           },
             {"story.flag_set",    {{"id", 0}, {"value", 2}}           },
             {"story.int_set",     {{"id", 0}, {"value", 2147483648LL}}},
             {"georama.get",       {{"town", 6}}                       }
    }) {
        EXPECT_FALSE(mods::ValidateOperation(c.operation, c.arguments).empty());
    }
}

TEST(ModApi, CapacityAndWeaponWritesAreBoundedAndAtomic) {
    CUserStatus status{};
    status.party_size = 1;
    status.hp[0] = status.max_hp[0] = 100;
    status.water_now[0] = status.water_max[0] = 30;
    auto &weapon = status.chara_weapons[0][0];
    weapon.item_no = 258;
    weapon.durability = 15;
    std::string error;
    ASSERT_TRUE(mods::ApplyStatus(status, {
                                              {"player.max_hp_set",      {{"max_hp", 50}}                  },
                                              {"player.max_water_set",   {{"max_water", 10.5}}             },
                                              {"player.defense_set",     {{"defense", 100}}                },
                                              {"weapons.durability_set", {{"slot", 0}, {"durability", 100}}}
    },
                                  0, error));
    EXPECT_EQ(status.hp[0], 50);
    EXPECT_EQ(status.next_hp[0], 50);
    EXPECT_EQ(status.water_now[0], 10.5);
    EXPECT_EQ(status.defense[0], 100);
    EXPECT_EQ(weapon.durability_f, 15);
    const auto before = status;
    EXPECT_FALSE(mods::ApplyStatus(status, {
                                               {"player.defense_set",     {{"defense", 1}}                 },
                                               {"weapons.durability_set", {{"slot", 1}, {"durability", 10}}}
    },
                                   0, error));
    EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
    EXPECT_FALSE(mods::ValidateOperation("player.max_hp_set", {
                                                                  {"max_hp", 0}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("player.max_water_set", {
                                                                     {"max_water", 0.5}
    })
                     .empty());
    EXPECT_FALSE(mods::ValidateOperation("weapons.durability_set", {
                                                                       {"durability", 1}
    })
                     .empty());
}

TEST(ModApi, StatusAddMatchesNativeForAllAilmentCombinationsAndPreservesRng) {
    const std::pair<const char *, int> ailments[] = {
        {"freeze",  AILMENT_FREEZE },
        {"stamina", AILMENT_STAMINA},
        {"poison",  AILMENT_POISON },
        {"curse",   AILMENT_CURSE  },
        {"goo",     AILMENT_GOO    }
    };
    for (int owner = 0; owner < 6; ++owner) {
        for (int bits = 0; bits < 64; ++bits) {
            for (const auto &[name, flag] : ailments) {
                SCOPED_TRACE(std::to_string(owner) + ":" + std::to_string(bits) + ":" + name);
                CUserStatus original{};
                original.party_size = 6;
                original.cur_chara = owner;
                original.ailments[owner] = (bits << 1) | 0x100; // Preserve unknown bits too.
                original.ailment_frames[owner] = 127;
                auto  native = original;
                auto *previous = UserStatus;
                UserStatus = &native;
                BtSetStatusErr(flag);
                UserStatus = previous;
                auto                actual = original;
                mods::CommitEffects effects;
                std::string         error;
                srand(812);
                const int next = rand();
                srand(812);
                const bool accepted = mods::ApplyStatus(actual, {
                                                                    {"player.status_add", {{"status", name}}}
                },
                                                        0, error, &effects);
                EXPECT_EQ(accepted, (native.ailments[owner] & flag) != 0);
                EXPECT_EQ(std::memcmp(&actual, accepted ? &native : &original, sizeof(actual)), 0);
                EXPECT_EQ(rand(), next);
                const bool sound = flag == AILMENT_FREEZE || flag == AILMENT_CURSE ||
                                   (flag == AILMENT_POISON && !(original.ailments[owner] & (AILMENT_FREEZE | AILMENT_STAMINA))) ||
                                   (flag == AILMENT_GOO && !(original.ailments[owner] & (AILMENT_FREEZE | AILMENT_STAMINA | AILMENT_POISON)));
                EXPECT_EQ(effects.status_sounds, accepted && sound ? 1u : 0u);
            }
        }
    }
}

TEST(ModApi, StatusRemovalPreservesOtherBitsAndOnlyLiveTimedEffectsKeepTheCounter) {
    CUserStatus status{};
    status.party_size = 2;
    status.cur_chara = 1;
    status.ailments[0] = AILMENT_POISON;
    status.ailments[1] = AILMENT_FREEZE | AILMENT_CURSE | AILMENT_KNOCKED_OUT;
    status.ailment_frames[1] = 123;
    std::string         error;
    mods::CommitEffects effects{42};
    ASSERT_TRUE(mods::ApplyStatus(status, {
                                              {"player.status_remove", {{"status", "curse"}, {"character", 1}}}
    },
                                  0, error, &effects));
    EXPECT_EQ(status.ailments[1], AILMENT_FREEZE | AILMENT_KNOCKED_OUT);
    EXPECT_EQ(status.ailment_frames[1], 123);
    EXPECT_EQ(effects.status_sounds, 0u);
    ASSERT_TRUE(mods::ApplyStatus(status, {
                                              {"player.status_remove", {{"status", "freeze"}}}
    },
                                  0, error));
    EXPECT_EQ(status.ailments[1], AILMENT_KNOCKED_OUT);
    EXPECT_EQ(status.ailment_frames[1], 0);
    EXPECT_EQ(status.ailments[0], AILMENT_POISON);
    const auto before = status;
    ASSERT_TRUE(mods::ApplyStatus(status, {
                                              {"player.status_remove", {{"status", "goo"}}}
    },
                                  0, error));
    EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
}

TEST(ModApi, StatusWritesAndNotificationsCommitAtomicallyWithOtherSaveCommands) {
    auto  save = std::make_unique<CSaveData>();
    auto *s = save->GetDngStatus();
    s->party_size = 1;
    s->cur_chara = 0;
    s->ailments[0] = 0;
    s->ailment_frames[0] = 0;
    const auto          before = std::make_unique<CSaveData>(*save);
    std::string         error;
    mods::CommitEffects effects{99};
    EXPECT_FALSE(mods::ApplySave(*save, {
                                            {"story.flag_set",    {{"id", 5}, {"value", 1}}},
                                            {"player.status_add", {{"status", "freeze"}}   },
                                            {"player.status_add", {{"status", "stamina"}}  }
    },
                                 0, error, &effects));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(effects.status_sounds, 0u);
    EXPECT_NE(error.find("native status rules rejected"), std::string::npos);
    ASSERT_TRUE(mods::ApplySave(*save, {
                                           {"player.status_add", {{"status", "poison"}}   },
                                           {"player.status_add", {{"status", "freeze"}}   },
                                           {"story.flag_set",    {{"id", 5}, {"value", 1}}}
    },
                                0, error, &effects));
    EXPECT_EQ(effects.status_sounds, 2u);
    EXPECT_EQ(s->ailments[0], AILMENT_FREEZE);
    EXPECT_EQ(s->ailment_frames[0], 300);
    EXPECT_EQ(save->GetGameFlag(5), 1);
    ASSERT_TRUE(mods::ApplySave(*save, {}, 0, error, &effects));
    EXPECT_EQ(effects.status_sounds, 0u);
}

TEST(ModApi, StatusValidationRejectsInventedEffectsTimersAndNonActiveTargets) {
    for (const auto &args : std::vector<Json>{
             Json::object(), {{"status", "knockout"}},
              {{"status", "freeze"}, {"duration_frames", 1}},
              {{"status", "Freeze"}},
              {{"status", 4}},
              {{"status", "curse"}, {"character", 6}},
              {{"status", "curse"}, {"character", true}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("player.status_add", args).empty()) << args;
        EXPECT_FALSE(mods::ValidateOperation("player.status_remove", args).empty()) << args;
    }
    CUserStatus s{};
    s.party_size = 2;
    s.cur_chara = 0;
    const auto          before = s;
    std::string         error;
    mods::CommitEffects effects;
    for (const auto *op : {"player.status_add", "player.status_remove"}) {
        EXPECT_FALSE(mods::ApplyStatus(s, {
                                              {op, {{"status", "poison"}, {"character", 1}}}
        },
                                       0, error, &effects));
        EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
        EXPECT_EQ(effects.status_sounds, 0u);
        s.cur_chara = 5;
        EXPECT_FALSE(mods::ApplyStatus(s, {
                                              {op, {{"status", "poison"}}}
        },
                                       0, error));
        s.cur_chara = 0;
    }
}

TEST(ModNative, Sha256MatchesStandardVectors) {
    EXPECT_EQ(mods::Sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(mods::Sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(mods::Sha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
TEST(ModApi, GeoramaSatisfactionIsDetachedBoundedAndReadOnly) {
    auto save = std::make_unique<CSaveData>();
    auto &town = *save->GetGrdData(0);
    town.request_count = 3;
    town.request_complete[0] = 1;
    town.request_complete[1] = 0;
    town.request_complete[2] = 2;
    const auto before = std::make_unique<CSaveData>(*save);
    srand(92); const auto next = rand(); srand(92);
    auto result = mods::ReadSave(*save, {}, "georama.satisfaction_get", {{"town", 0}});
    EXPECT_EQ(result["request_count"], 3);
    EXPECT_EQ(result["completed_requests"], 2);
    EXPECT_EQ(result["satisfaction_percent"], 67);
    EXPECT_EQ(result["requests"][1]["completed"], false);
    result["requests"][0]["completed"] = false;
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
    town.request_count = 0;
    EXPECT_EQ(mods::ReadSave(*save, {}, "georama.satisfaction_get", {{"town", 0}})["satisfaction_percent"], 0);
    for (int count : {-1, 25}) {
        town.request_count = count;
        EXPECT_THROW(mods::ReadSave(*save, {}, "georama.satisfaction_get", {{"town", 0}}), std::runtime_error);
    }
    for (int id : {-1, 5}) EXPECT_FALSE(mods::ValidateOperation("georama.satisfaction_get", {{"town", id}}).empty());
    EXPECT_FALSE(mods::ValidateOperation("georama.satisfaction_get", {{"town", 0}, {"extra", 1}}).empty());
}
} // namespace
