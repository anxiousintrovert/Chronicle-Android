#include <gtest/gtest.h>

#include <cstring>
#include <limits>

#include "battlemenu.hpp"
#include "frame.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "mods/api.hpp"
#include "savedata.hpp"
#include "weapon_buildup.hpp"
#include "weaponlevelup.hpp"

namespace {
using Json = nlohmann::json;
using mods::GameCommand;

class ModRetailInventory : public testing::Test {
protected:
    std::unique_ptr<CSaveData> save;
    std::string                error;

    void SetUp() override { save = std::make_unique<CSaveData>(); }

    CUserStatus &Status() { return *save->GetDngStatus(); }

    bool Apply(std::vector<GameCommand> commands) { return mods::ApplySave(*save, commands, 0, error); }

    Json Read(const char *name, Json args) { return mods::ReadSave(*save, {1, 0, 0, true}, name, args); }
};

TEST_F(ModRetailInventory, BagGrantsUseNativeDefinitionsAndCountQuickSlots) {
    auto &s = Status();
    s.item_pack.num = 4;
    s.item_pack.quick_item_slot[0] = ITEM_BREAD;
    s.item_pack.quick_item_qty[0] = 2;
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_BREAD}, {"quantity", 2}, {"seed", 9}}}
    }))
        << error;
    EXPECT_EQ(Read("inventory.count", {
                                          {"item", ITEM_BREAD}
    })["quantity"],
              4);
    EXPECT_EQ(s.item_pack.item_count, 4);
    EXPECT_EQ(s.item_pack.item_vol[0], GetItemData(ITEM_BREAD)->vol);
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"inventory.give", {{"item", ITEM_BREAD}, {"quantity", 1}, {"seed", 9}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    ASSERT_TRUE(Apply({
        {"inventory.remove", {{"item", ITEM_BREAD}, {"quantity", 3}}}
    }))
        << error;
    EXPECT_EQ(s.item_pack.item[0], -1);
    EXPECT_EQ(s.item_pack.quick_item_qty[0], 1);
    EXPECT_EQ(s.item_pack.item_count, 1);
}

TEST_F(ModRetailInventory, FailedGrantBatchRollsBackItemsMoneyAndGlobalRng) {
    const auto before = std::make_unique<CSaveData>(*save);
    srand(401);
    const int next = rand();
    srand(401);
    EXPECT_FALSE(Apply({
        {"inventory.give",   {{"item", ITEM_DRAN_S_FEATHER}, {"quantity", 1}, {"seed", 123}}},
        {"economy.add",      {{"amount", 50}}                                               },
        {"inventory.remove", {{"item", ITEM_BREAD}, {"quantity", 100}}                      }
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModRetailInventory, ExplicitGrantSeedIsRepeatableAndNeverConsumesRetailRng) {
    auto                           copy = std::make_unique<CSaveData>(*save);
    const std::vector<GameCommand> commands = {
        {"inventory.give", {{"item", ITEM_DRAN_S_FEATHER}, {"quantity", 2}, {"seed", 4294967295ULL}}},
        {"weapons.give",   {{"item", ITEM_WEAPON_BONE_RAPIER}, {"seed", 81}}                        }
    };
    srand(11);
    const int next = rand();
    srand(11);
    ASSERT_TRUE(Apply(commands)) << error;
    EXPECT_EQ(rand(), next);
    ASSERT_TRUE(mods::ApplySave(*copy, commands, 0, error)) << error;
    EXPECT_EQ(std::memcmp(save.get(), copy.get(), sizeof(CSaveData)), 0);
    EXPECT_GE(Status().item_pack.item_vol[0], 180 * 60);
    EXPECT_LE(Status().item_pack.item_vol[0], 359 * 60);
    const auto *d = GetWeaponData(ITEM_WEAPON_BONE_RAPIER);
    EXPECT_EQ(Status().chara_weapons[d->owner][1].attack, d->attack);
}

TEST_F(ModRetailInventory, AbsThresholdAndSynthesisAreBoundedWithoutLevelUp) {
    const auto before = Read("weapons.abs_get", {
                                                    {"slot", 0}
    });
    ASSERT_TRUE(Apply({
        {"weapons.abs_add", {{"slot", 0}, {"amount", 32767}, {"synthesis", 7}}}
    }))
        << error;
    const auto after = Read("weapons.abs_get", {
                                                   {"slot", 0}
    });
    EXPECT_EQ(after["abs"], after["threshold"]);
    EXPECT_EQ(after["level"], before["level"]);
    EXPECT_EQ(after["synthesis"], 7);
    Status().chara_weapons[0][0].synthesis_count = std::numeric_limits<int32_t>::max();
    EXPECT_FALSE(Apply({
        {"weapons.abs_add", {{"slot", 0}, {"amount", 0}, {"synthesis", 1}}}
    }));
    EXPECT_EQ(Status().chara_weapons[0][0].synthesis_count, std::numeric_limits<int32_t>::max());
}

TEST_F(ModRetailInventory, WorldMapOwnershipUsesNativeFlagNotBagSpace) {
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_WORLDMAP}, {"quantity", 1}, {"seed", 0}}}
    }))
        << error;
    EXPECT_EQ(Read("inventory.count", {
                                          {"item", ITEM_WORLDMAP}
    })["quantity"],
              1);
    EXPECT_EQ(Status().item_pack.item_count, 0);
    ASSERT_TRUE(Apply({
        {"inventory.remove", {{"item", ITEM_WORLDMAP}, {"quantity", 1}}}
    }))
        << error;
    EXPECT_EQ(Status().special_flag_238, 0);
    EXPECT_FALSE(Apply({
        {"inventory.remove", {{"item", ITEM_WORLDMAP}, {"quantity", 1}}}
    }));
}

TEST_F(ModRetailInventory, WeaponCapacityAndMissingOwnersRejectWithoutOverflow) {
    auto &s = Status();
    for (auto &w : s.chara_weapons[0]) {
        w.item_no = ITEM_WEAPON_DAGGER;
    }
    const auto before = s;
    EXPECT_FALSE(Apply({
        {"weapons.give", {{"item", ITEM_WEAPON_BONE_RAPIER}, {"seed", 1}}}
    }));
    EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
    EXPECT_FALSE(Apply({
        {"weapons.give", {{"item", ITEM_WEAPON_BONE_SLINGSHOT}, {"seed", 1}}}
    }));
    EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
}

TEST(ModRetailValidation, InvalidRetailIdsAndAmountsNeverReachLookupTables) {
    for (const auto &c : std::vector<GameCommand>{
             {"inventory.give",          {{"item", 131}, {"quantity", 1}, {"seed", 0}}},
             {"inventory.give",          {{"item", 148}, {"quantity", 1}}             },
             {"inventory.remove",        {{"item", 148}, {"quantity", -1}}            },
             {"weapons.give",            {{"item", 377}, {"seed", 0}}                 },
             {"weapons.give",            {{"item", 258}, {"seed", 4294967296ULL}}     },
             {"weapons.abs_add",         {{"slot", 11}, {"amount", 1}}                },
             {"items.native_definition", {{"item", "author.custom-blade"}}            }
    }) {
        EXPECT_FALSE(mods::ValidateOperation(c.operation, c.arguments).empty());
    }
}

TEST_F(ModRetailInventory, AttachmentBagPreservesNativeRecordsAndRejectsOverflowAtomically) {
    EXPECT_THROW(Read("items.native_definition", {
                                                     {"item", 131}
    }),
                 std::runtime_error);
    const auto definition = Read("items.native_definition", {
                                                                {"item", ITEM_ATTACH_FIRE}
    });
    ASSERT_TRUE(Apply({
        {"attachments.give", {{"item", ITEM_ATTACH_FIRE}, {"quantity", 2}}}
    }))
        << error;
    auto bag = Read("attachments.list", Json::object());
    EXPECT_EQ(bag["capacity"], 40);
    ASSERT_EQ(bag["slots"].size(), 43u);
    EXPECT_EQ(bag["slots"][0]["item"], ITEM_ATTACH_FIRE);
    EXPECT_EQ(bag["slots"][0]["elements"], definition["elements"]);
    EXPECT_EQ(bag["slots"][0]["elements"][0], 3);
    EXPECT_EQ(bag["slots"][40]["overflow"], true);
    ASSERT_TRUE(Apply({
        {"attachments.remove", {{"bag_slot", 0}}}
    }))
        << error;
    EXPECT_EQ(Read("attachments.list", Json::object())["slots"][0]["item"], -1);
    EXPECT_FALSE(Apply({
        {"attachments.remove", {{"bag_slot", 0}}}
    }));
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"attachments.give", {{"item", ITEM_ATTACH_FIRE}, {"quantity", 40}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    Status().consumable_items[40] = Status().consumable_items[1];
    EXPECT_FALSE(Apply({
        {"attachments.give", {{"item", ITEM_ATTACH_FIRE}, {"quantity", 1}}}
    }));
    EXPECT_NE(error.find("overflow"), std::string::npos);
    ASSERT_TRUE(Apply({
        {"attachments.remove", {{"bag_slot", 40}}}
    }))
        << error;
    EXPECT_FALSE(Apply({
        {"attachments.give", {{"item", ITEM_ATTACH_SYNTHESIS_SPHERE}, {"quantity", 1}}}
    }));
    EXPECT_NE(error.find("owned weapons"), std::string::npos);
    ASSERT_TRUE(Apply({
        {"attachments.give", {{"item", ITEM_ATTACH_ATTACK}, {"quantity", 1}, {"level", 3}}}
    }))
        << error;
    EXPECT_EQ(Read("attachments.list", Json::object())["slots"][0]["stats"][0], 3);
}

TEST_F(ModRetailInventory, StatusBreakUsesNativeTwoStageRoundingSocketMultipliersAndFlags) {
    auto &s = Status();
    auto &w = s.chara_weapons[0][1];
    WepDataListToHaveCopy(ITEM_WEAPON_BONE_RAPIER, &w);
    w.level = 5;
    w.attack = 3;
    w.endurance = 15;
    w.speed = 4;
    w.magic = 11;
    w.elem[0] = 11;
    w.vs_monster[0] = 9;
    w.flags = WEAPONFLAG_DURABLE;
    auto &fitted = w.attach[0];
    fitted.item_no = ITEM_ATTACH_SYNTHESIS_SPHERE;
    fitted.status[0] = 1;
    fitted.elem[0] = 4;
    fitted.vs_monster[0] = 5;
    fitted.sphere_flags = WEAPONFLAG_FRAGILE | WEAPONFLAG_QUENCH;
    w.attach_kind[0] = 3;
    srand(831);
    const int next = rand();
    srand(831);
    ASSERT_TRUE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }))
        << error;
    EXPECT_EQ(rand(), next);
    const auto sphere = Read("attachments.list", Json::object())["slots"][0];
    EXPECT_EQ(sphere["item"], ITEM_ATTACH_SYNTHESIS_SPHERE);
    EXPECT_EQ(sphere["sphere_weapon"], ITEM_WEAPON_BONE_RAPIER);
    EXPECT_EQ(sphere["sphere_level"], 5);
    EXPECT_EQ(sphere["sphere_flags"], WEAPONFLAG_QUENCH);
    EXPECT_EQ(sphere["stats"], (Json::array({2, 9, 2, 6})));
    EXPECT_EQ(sphere["elements"][0], 10);
    EXPECT_EQ(sphere["monster_stats"][0], 11);
    EXPECT_EQ(s.chara_weapons[0][1].item_no, -1);
    EXPECT_EQ(s.equipped_weapon_slot[0], 0);
    EXPECT_FALSE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }));
}

TEST_F(ModRetailInventory, StatusBreakRejectionsAndLaterFailureKeepWholeSaveUnchanged) {
    auto &s = Status();
    auto &w = s.chara_weapons[0][1];
    WepDataListToHaveCopy(ITEM_WEAPON_BONE_RAPIER, &w);
    w.level = 4;
    EXPECT_FALSE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }));
    w.level = 5;
    s.equipped_weapon_slot[0] = 1;
    EXPECT_FALSE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }));
    s.equipped_weapon_slot[0] = 0;
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"weapons.synthesize", {{"slot", 1}}      },
        {"economy.spend",      {{"amount", 32767}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    for (auto &attachment : s.consumable_items) {
        attachment.id = ITEM_ATTACH_FIRE;
    }
    const auto full = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), full.get(), sizeof(CSaveData)), 0);
}

TEST_F(ModRetailInventory, StatusBreakResetsDefaultWeaponWithoutDeletingOrUsingRng) {
    auto       &w = Status().chara_weapons[0][1];
    WEAPON_HAVE expected{};
    WepDataListToHaveCopy(ITEM_WEAPON_DAGGER, &expected);
    w = expected;
    w.level = 5;
    w.attack += 20;
    srand(904);
    const int next = rand();
    srand(904);
    ASSERT_TRUE(Apply({
        {"weapons.synthesize", {{"slot", 1}}}
    }))
        << error;
    EXPECT_EQ(rand(), next);
    EXPECT_EQ(std::memcmp(&w, &expected, sizeof(w)), 0);
    EXPECT_EQ(Read("attachments.list", Json::object())["slots"][0]["sphere_level"], 5);
}

TEST_F(ModRetailInventory, BuildUpInspectionMatchesRetailTargetsAndRequirementsForAllDefinitionsWithoutRng) {
    auto &s = Status();
    s.party_size = 6;
    for (int item = ITEM_WEAPON_START; item < ITEM_WEAPON_END; ++item) {
        if (item == ITEM_WEAPON_MARDAN_EINS || item == ITEM_WEAPON_MARDAN_TWEI) {
            continue;
        }
        const auto *d = GetWeaponData(item);
        ASSERT_NE(d, nullptr);
        ASSERT_LT(d->owner, 6);
        auto &w = s.chara_weapons[d->owner][0];
        WepDataListToHaveCopy(item, &w);
        for (const bool high : {false, true}) {
            if (high) {
                w.attack = d->attack_max;
                w.magic = d->magic_max;
                for (auto &value : w.elem) {
                    value = 99;
                }
                for (auto &value : w.vs_monster) {
                    value = 99;
                }
            }
            WEP_BUILDUP_INFO native[128]{};
            EnableBuildUpModel(native, &w);
            const auto before = std::make_unique<CSaveData>(*save);
            srand(73);
            const int next = rand();
            srand(73);
            const auto result = Read("weapons.build_up_list", {
                                                                  {"slot",      0       },
                                                                  {"character", d->owner}
            });
            EXPECT_EQ(rand(), next);
            EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
            int count = 0;
            while (count < 128 && native[count].weapon_no != -1) {
                ++count;
            }
            ASSERT_EQ(result.size(), count) << item;
            for (int i = 0; i < count; ++i) {
                EXPECT_EQ(result[i]["item"], native[i].weapon_no) << item;
                EXPECT_EQ(result[i]["meets_requirements"], native[i].enabled != 0) << item;
                EXPECT_EQ(result[i]["can_build"], native[i].enabled != 0) << item;
                EXPECT_EQ(result[i]["requirements"]["elements"].size(), 5u);
                EXPECT_TRUE(result[i]["requirements"]["elements"].is_array());
            }
        }
    }
}

TEST_F(ModRetailInventory, MardanBuildUpsUseThisSaveAndReportAttachmentPrerequisite) {
    auto &w = Status().chara_weapons[0][0];
    for (const int item : {ITEM_WEAPON_MARDAN_EINS, ITEM_WEAPON_MARDAN_TWEI}) {
        WepDataListToHaveCopy(item, &w);
        const int needed = item == ITEM_WEAPON_MARDAN_EINS ? 5 : 15;
        save->SetGameIntFlag(0x14, needed - 1);
        auto result = Read("weapons.build_up_list", {
                                                        {"slot", 0}
        });
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(result[0]["item"], item + 1);
        EXPECT_EQ(result[0]["can_build"], false);
        save->SetGameIntFlag(0x14, needed);
        EXPECT_EQ(Read("weapons.build_up_list", {
                                                    {"slot", 0}
        })[0]["can_build"],
                  true);
        w.attach[0].item_no = ITEM_ATTACH_FIRE;
        result = Read("weapons.build_up_list", {
                                                   {"slot", 0}
        });
        EXPECT_EQ(result[0]["meets_requirements"], true);
        EXPECT_EQ(result[0]["attachments_empty"], false);
        EXPECT_EQ(result[0]["can_build"], false);
    }
}

TEST(ModRetailValidation, AttachmentAndSynthesisArgumentsRejectUnknownFieldsAndUnsafeSlots) {
    EXPECT_FALSE(mods::ValidateOperation("attachments.give", {
                                                                 {"item",     131},
                                                                 {"quantity", 1  }
    })
                     .empty());
    for (const mods::GameCommand &c : std::vector<mods::GameCommand>{
             {"attachments.list",      {{"item", 81}}                               },
             {"attachments.give",      {{"item", 91}, {"quantity", 1}, {"level", 4}}},
             {"attachments.give",      {{"item", 81}, {"quantity", 1}, {"level", 1}}},
             {"attachments.give",      {{"item", 132}, {"quantity", 1}}             },
             {"attachments.give",      {{"item", 81}, {"quantity", 41}}             },
             {"attachments.remove",    {{"bag_slot", 43}}                           },
             {"attachments.remove",    {{"bag_slot", -1}}                           },
             {"attachments.remove",    {{"bag_slot", 0.5}}                          },
             {"weapons.build_up_list", {{"slot", 11}}                               },
             {"weapons.synthesize",    {{"slot", 0}, {"character", 6}}              },
             {"weapons.synthesize",    {{"slot", 0}, {"force", true}}               },
    }) {
        EXPECT_FALSE(mods::ValidateOperation(c.operation, c.arguments).empty()) << c.operation;
    }
}
} // namespace

namespace {
TEST_F(ModRetailInventory, BuildUpMatchesNativeCompletionAcrossEligibleRetailTargetsWithoutRng) {
    auto &s = Status();
    s.party_size = 6;
    int checked = 0;
    for (int item = ITEM_WEAPON_START; item < ITEM_WEAPON_END; ++item) {
        const auto *definition = GetWeaponData(item);
        const int   owner = definition->owner;
        s.equipped_weapon_slot[owner] = 0;
        auto &weapon = s.chara_weapons[owner][1];
        WepDataListToHaveCopy(item, &weapon);
        weapon.attack = definition->attack_max;
        weapon.magic = definition->magic_max;
        weapon.level = 7;
        weapon.experience = 17;
        for (auto &v : weapon.elem) {
            v = 99;
        }
        for (auto &v : weapon.vs_monster) {
            v = 99;
        }
        save->SetGameIntFlag(0x14, 20);
        const auto initial = weapon;
        const auto targets = Read("weapons.build_up_list", {
                                                               {"character", owner},
                                                               {"slot",      1    }
        });
        for (const auto &target : targets) {
            if (!target.at("can_build").get<bool>()) {
                continue;
            }
            weapon = initial;
            save->SetGameIntFlag(0x14, 20);
            auto expected = initial;
            // Execute the linked retail effect's completion branch on a detached
            // weapon. A real frame object supplies only the non-null readiness gate.
            CFrame         frame;
            CWeaponLevelUp native{};
            native.operation_kind = WEP_EFFECT_BUILDUP;
            native.effect_state = WEP_EFFECT_STATE_BUILDUP_PLAY;
            native.effect.frame = &frame;
            native.effect.motion_state = 3;
            native.weapon = &expected;
            native.buildup_weapon_no = target.at("item").get<int>();
            const auto texture_flag = MenuExTextureReadFlag;
            native.Step();
            MenuExTextureReadFlag = texture_flag;
            ASSERT_EQ(native.buildup_complete, 1);
            srand(314);
            const int next = rand();
            srand(314);
            ASSERT_TRUE(Apply({
                {"weapons.build_up", {{"character", owner}, {"slot", 1}, {"target", target.at("item")}}}
            }))
                << error;
            EXPECT_EQ(rand(), next);
            EXPECT_EQ(std::memcmp(&weapon, &expected, sizeof(weapon)), 0) << item;
            EXPECT_EQ(save->GetGameIntFlag(0x14), (item == ITEM_WEAPON_MARDAN_EINS || item == ITEM_WEAPON_MARDAN_TWEI) ? 0 : 20);
            ++checked;
        }
    }
    EXPECT_GT(checked, 60);
}

TEST_F(ModRetailInventory, BuildUpRechecksRequirementsAndRollsBackWholeSaveAndFishCounter) {
    auto &s = Status();
    auto &w = s.chara_weapons[0][1];
    s.equipped_weapon_slot[0] = 0;
    WepDataListToHaveCopy(ITEM_WEAPON_MARDAN_EINS, &w);
    const GameCommand build{
        "weapons.build_up", {{"slot", 1}, {"target", ITEM_WEAPON_MARDAN_TWEI}}
    };
    auto reject = [&](std::vector<GameCommand> commands) {
        const auto before = std::make_unique<CSaveData>(*save);
        EXPECT_FALSE(Apply(std::move(commands)));
        EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    };
    save->SetGameIntFlag(0x14, 4);
    reject({build});
    save->SetGameIntFlag(0x14, 5);
    s.equipped_weapon_slot[0] = 1;
    reject({build});
    s.equipped_weapon_slot[0] = 0;
    w.attach[0].item_no = ITEM_ATTACH_FIRE;
    reject({build});
    w.attach[0].item_no = 0;
    reject({
        {"weapons.build_up", {{"slot", 1}, {"target", ITEM_WEAPON_START}}}
    });
    reject({
        build, {"economy.spend", {{"amount", 65535}}}
    });
    reject({build, build});
    ASSERT_TRUE(Apply({
        {"story.int_set", {{"id", 0x14}, {"value", 5}}},
        build
    }))
        << error;
    EXPECT_EQ(w.item_no, ITEM_WEAPON_MARDAN_TWEI);
    EXPECT_EQ(save->GetGameIntFlag(0x14), 0);
}

TEST(ModRetailValidation, BuildUpRequiresBoundedExactArgumentsAndSaveHost) {
    EXPECT_TRUE(mods::ValidateOperation("weapons.build_up", {
                                                                {"slot",   1  },
                                                                {"target", 260}
    })
                    .empty());
    for (const auto &args : std::vector<Json>{
             {{"slot", 1}},
             {{"slot", 11}, {"target", 260}},
             {{"slot", 1}, {"target", 377}},
             {{"slot", 1}, {"target", 260.5}},
             {{"slot", 1}, {"target", 260}, {"force", true}},
             {{"slot", 1}, {"target", 260}, {"character", 6}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("weapons.build_up", args).empty());
    }
    CUserStatus s{};
    const auto  before = s;
    std::string error;
    EXPECT_FALSE(mods::ApplyStatus(s, {
                                          {"weapons.build_up", {{"slot", 1}, {"target", 260}}}
    },
                                   0, error));
    EXPECT_NE(error.find("save host"), std::string::npos);
    EXPECT_EQ(std::memcmp(&s, &before, sizeof(s)), 0);
}
} // namespace

namespace {
TEST_F(ModRetailInventory, UpgradeMatchesRetailBaseStatCompletionAcrossDefinitionsAndKeepsGlobalRng) {
    auto &s = Status();
    s.party_size = 6;
    for (int item = ITEM_WEAPON_START; item < ITEM_WEAPON_END; ++item) {
        const auto *d = GetWeaponData(item);
        s.equipped_weapon_slot[d->owner] = 0;
        auto &w = s.chara_weapons[d->owner][1];
        WepDataListToHaveCopy(item, &w);
        w.experience = GetWeaponMaxExp(&w);
        w.durability_f = 1.5f;
        w.synthesis_count = 3;
        const auto     initial = w;
        auto           expected = w;
        CWeaponLevelUp native{};
        native.weapon = &expected;
        native.SetLevelUpWeaponData();
        srand(81);
        const int next = rand();
        srand(81);
        ASSERT_TRUE(Apply({
            {"weapons.upgrade", {{"character", d->owner}, {"slot", 1}, {"seed", 27}}}
        }))
            << error;
        EXPECT_EQ(rand(), next);
        EXPECT_EQ(w.attack, expected.attack) << item;
        EXPECT_EQ(w.magic, expected.magic) << item;
        EXPECT_EQ(w.endurance, expected.endurance) << item;
        EXPECT_EQ(w.speed, expected.speed) << item;
        EXPECT_EQ(w.experience, expected.experience) << item;
        EXPECT_EQ(w.item_no, item);
        EXPECT_EQ(w.level, initial.level + 1);
        EXPECT_EQ(w.synthesis_count, 3);
        EXPECT_EQ(w.durability_f, 1.5f);
        EXPECT_GE(w.durability, std::min(static_cast<int>(initial.durability) + 1, 99));
        EXPECT_LE(w.durability, std::min(static_cast<int>(initial.durability) + 3, 99));
        EXPECT_EQ(std::memcmp(w.elem, initial.elem, sizeof(w.elem)), 0);
        EXPECT_EQ(std::memcmp(w.vs_monster, initial.vs_monster, sizeof(w.vs_monster)), 0);
        int doubled = 0;
        for (int i = 0; i < 5; ++i) {
            if (d->hole[i] > 0 && d->hole[i] != 2) {
                EXPECT_TRUE(w.attach_kind[i] == 0 || w.attach_kind[i] == 3);
                doubled += w.attach_kind[i] == 3;
            } else {
                EXPECT_EQ(w.attach_kind[i], initial.attach_kind[i]);
            }
        }
        EXPECT_LE(doubled, 1);
        const auto first = w;
        w = initial;
        ASSERT_TRUE(Apply({
            {"weapons.upgrade", {{"character", d->owner}, {"slot", 1}, {"seed", 27}}}
        }))
            << error;
        EXPECT_EQ(std::memcmp(&w, &first, sizeof(w)), 0);
    }
}

TEST_F(ModRetailInventory, UpgradeConsumesPowderOnlyWhenNeededAndRollsBackPaymentOnFailure) {
    auto &s = Status();
    auto &w = s.chara_weapons[0][1];
    s.equipped_weapon_slot[0] = 0;
    WepDataListToHaveCopy(ITEM_WEAPON_MARDAN_EINS, &w);
    const GameCommand upgrade{
        "weapons.upgrade", {{"slot", 1}, {"seed", 91}}
    };
    auto reject = [&](std::vector<GameCommand> commands) {
        const auto before = std::make_unique<CSaveData>(*save);
        EXPECT_FALSE(Apply(std::move(commands)));
        EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    };
    reject({upgrade});
    s.item_pack.num = 3;
    ASSERT_TRUE(Apply({
        {"inventory.give", {{"item", ITEM_POWERUP_POWDER}, {"quantity", 1}, {"seed", 1}}}
    }))
        << error;
    reject({
        upgrade, {"economy.spend", {{"amount", 65535}}}
    });
    s.equipped_weapon_slot[0] = 1;
    reject({upgrade});
    s.equipped_weapon_slot[0] = 0;
    w.level = 99;
    reject({upgrade});
    w.level = 0;
    ASSERT_TRUE(Apply({upgrade})) << error;
    EXPECT_EQ(w.level, 1);
    EXPECT_EQ(Read("inventory.count", {
                                          {"item", ITEM_POWERUP_POWDER}
    })["quantity"],
              0);
    s.item_pack.quick_item_slot[0] = ITEM_POWERUP_POWDER;
    s.item_pack.quick_item_qty[0] = 1;
    s.item_pack.item_count = 1;
    ASSERT_TRUE(Apply({
        {"weapons.abs_add", {{"slot", 1}, {"amount", 32767}}},
        upgrade
    }))
        << error;
    EXPECT_EQ(w.level, 2);
    EXPECT_EQ(s.item_pack.quick_item_qty[0], 1);
    ASSERT_TRUE(Apply({upgrade})) << error;
    EXPECT_EQ(w.level, 3);
    EXPECT_EQ(s.item_pack.quick_item_qty[0], 0);
}

TEST_F(ModRetailInventory, UpgradeAbsorbsDoubledAttachmentsAndSpheresWithRetailCaps) {
    auto &s = Status();
    s.equipped_weapon_slot[0] = 0;
    auto &w = s.chara_weapons[0][1];
    int   item = ITEM_WEAPON_START;
    for (; item < ITEM_WEAPON_END; ++item) {
        const auto *d = GetWeaponData(item);
        if (d->owner == 0 && d->hole[0] > 0 && d->hole[1] > 0) {
            break;
        }
    }
    ASSERT_LT(item, ITEM_WEAPON_END);
    WepDataListToHaveCopy(item, &w);
    w.attack = GetWeaponData(item)->attack_max - 1;
    w.endurance = 90;
    w.speed = 90;
    w.magic = GetWeaponData(item)->magic_max - 1;
    w.elem[0] = 90;
    w.vs_monster[0] = 90;
    w.durability = 99;
    w.experience = GetWeaponMaxExp(&w);
    AttachDataListToHaveCopy(ITEM_ATTACH_FIRE, &w.attach[0]);
    w.attach_kind[0] = 3;
    w.attach[0].elem[1] = 4;
    w.elem[1] = 10;
    w.attach[1] = {};
    w.attach[1].item_no = ITEM_ATTACH_SYNTHESIS_SPHERE;
    w.attach[1].sphere_weapon_no = item;
    for (auto &v : w.attach[1].status) {
        v = 20;
    }
    w.attach[1].vs_monster[0] = 20;
    w.attach[1].elem[0] = 20;
    w.synthesis_count = std::numeric_limits<int32_t>::max();
    const auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        {"weapons.upgrade", {{"slot", 1}, {"seed", 91}}}
    }));
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    w.synthesis_count = 4;
    ASSERT_TRUE(Apply({
        {"weapons.upgrade", {{"slot", 1}, {"seed", 91}}}
    }))
        << error;
    EXPECT_EQ(w.synthesis_count, 5);
    EXPECT_EQ(w.attack, GetWeaponData(item)->attack_max);
    EXPECT_EQ(w.magic, GetWeaponData(item)->magic_max);
    EXPECT_EQ(w.endurance, 99);
    EXPECT_EQ(w.speed, 99);
    EXPECT_EQ(w.elem[0], 99);
    EXPECT_EQ(w.elem[1], 18);
    EXPECT_EQ(w.vs_monster[0], 99);
    EXPECT_EQ(w.durability, 99);
    for (const auto &attachment : w.attach) {
        const ATTACH_LIST empty{};
        EXPECT_EQ(std::memcmp(&attachment, &empty, sizeof(empty)), 0);
    }
}

TEST(ModRetailValidation, UpgradeRequiresExplicitSeedAndRejectsUnknownArguments) {
    EXPECT_TRUE(mods::ValidateOperation("weapons.upgrade", {
                                                               {"slot", 1          },
                                                               {"seed", 4294967295u}
    })
                    .empty());
    for (const auto &args : std::vector<Json>{
             {{"slot", 1}},
             {{"slot", 1}, {"seed", -1}},
             {{"slot", 1}, {"seed", 4294967296ull}},
             {{"slot", 1}, {"seed", 1.5}},
             {{"slot", 11}, {"seed", 1}},
             {{"slot", 1}, {"seed", 1}, {"levels", 2}},
             {{"slot", 1}, {"seed", 1}, {"character", 6}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("weapons.upgrade", args).empty());
    }
}
} // namespace
