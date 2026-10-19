#include <gtest/gtest.h>

#include <cstring>

#include "mods/api.hpp"
#include "mods/native.hpp"
#include "savedata.hpp"
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
        if (!op.writes && !op.name.starts_with("story.") && !op.name.starts_with("map.") && !op.name.starts_with("fishing.") && !op.name.starts_with("georama.")) {
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
    auto       save = std::make_unique<CSaveData>();
    const auto before = std::make_unique<CSaveData>(*save);
    srand(811);
    const auto expected = rand();
    srand(811);
    for (const auto &op : mods::Operations()) {
        if (op.writes) {
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
        if (op.name == "georama.get") {
            a["town"] = 0;
        }
        EXPECT_NO_THROW(mods::ReadSave(*save, {1, 0, 0, true}, op.name, a));
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

TEST(ModNative, Sha256MatchesStandardVectors) {
    EXPECT_EQ(mods::Sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(mods::Sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(mods::Sha256(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
} // namespace
