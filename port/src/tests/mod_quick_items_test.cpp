#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "framevu1.hpp"
#include "main.hpp"
#include "mainitemmodel.hpp"
#include "mods/api.hpp"
#include "mods/entities.hpp"
#include "mods/quick_items.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

extern s32 mode, gameTask, itemNowSel;

namespace {
class ModQuickItems : public testing::Test {
protected:
    int                             old_mode = mode, old_task = gameTask, old_draw = BtAllDrawFlag, old_game = BtGameModeFlag, old_event = BtEventMode, old_selection = itemNowSel;
    CSaveData                      *old_save = SaveData;
    CUserStatus                    *old_user = UserStatus;
    CMonstorUnit                   *old_unit = NowMonstorUnit;
    CFrame                         *old_frame = CharaMain.frame;
    BT_ACT_STATUS                   old_battle = BtActStatus;
    CActiveItemPack                 old_items = activeItem;
    std::unique_ptr<CSaveData>      save = std::make_unique<CSaveData>();
    std::unique_ptr<CMonstorUnit>   unit = std::make_unique<CMonstorUnit>();
    std::unique_ptr<CMainItemModel> pool = std::make_unique<CMainItemModel>();
    CFrameVu1                       frame;
    unsigned int                    cache_data = 0;
    std::string                     error;

    void SetUp() override {
        SaveData = save.get();
        UserStatus = save->GetDngStatus();
        NowMonstorUnit = unit.get();
        CharaMain.frame = &frame;
        BtAllDrawFlag = 1;
        mode = GAME_MODE_DUNGEON;
        gameTask = GAME_TASK_PLAY;
        BtGameModeFlag = BT_GAME_MODE_PLAY;
        BtEventMode = 0;
        BtActStatus = {};
        BtActStatus.can_act = 1;
        UserStatus->party_size = 1;
        UserStatus->cur_chara = 0;
        UserStatus->money = 10;
        activeItem = {};
        activeItem.models = pool.get();
        for (int i = 0; i < 3; ++i) {
            const int item = ITEM_REGULAR_WATER + i;
            UserStatus->item_pack.quick_item_slot[i] = item;
            UserStatus->item_pack.quick_item_qty[i] = 1;
            activeItem.item[i + 1] = item;
            activeItem.model[i + 1] = i;
            pool->model[i] = ITEM_MODEL_LOADED;
            pool->model_cash[i] = i;
            pool->cash_item[i] = item;
            pool->cash[i] = &cache_data;
        }
        itemNowSel = activeItem.now = 1;
    }

    void TearDown() override {
        mode = old_mode;
        gameTask = old_task;
        BtAllDrawFlag = old_draw;
        BtGameModeFlag = old_game;
        BtEventMode = old_event;
        itemNowSel = old_selection;
        SaveData = old_save;
        UserStatus = old_user;
        NowMonstorUnit = old_unit;
        CharaMain.frame = old_frame;
        BtActStatus = old_battle;
        activeItem = old_items;
    }

    mods::GameCommand Select(int slot) { return {"items.quick_slot_select", {{"slot", slot}}, "test.quick"}; }

    bool Apply(std::vector<mods::GameCommand> commands) {
        return mods::ApplyEntityAndSave(*save, unit.get(), commands, 0, error, nullptr, nullptr);
    }
};

TEST_F(ModQuickItems, SelectionPreservesSaveResourcesAndRng) {
    auto before = std::make_unique<CSaveData>(*save);
    auto models = std::make_unique<CMainItemModel>(*pool);
    srand(357);
    const int next = rand();
    srand(357);
    for (int slot : {2, 3, 1}) {
        ASSERT_TRUE(Apply({Select(slot)})) << error;
        EXPECT_EQ(itemNowSel, slot);
        EXPECT_EQ(activeItem.now, slot);
        auto state = mods::ReadQuickItems();
        EXPECT_TRUE(state["ready"]);
        EXPECT_EQ(state["selected"], slot);
        EXPECT_TRUE(state["slots"][slot - 1]["ready"]);
    }
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_EQ(std::memcmp(static_cast<const void *>(pool.get()), static_cast<const void *>(models.get()), sizeof(CMainItemModel)), 0);
    EXPECT_EQ(rand(), next);
}

TEST_F(ModQuickItems, LaterFailuresAndInventoryInvalidationRollbackSelectionAndSave) {
    auto before = std::make_unique<CSaveData>(*save);
    EXPECT_FALSE(Apply({
        Select(2), {"fishing.spend", {{"amount", 9999}}, "test.quick"}
    }));
    EXPECT_EQ(itemNowSel, 1);
    EXPECT_EQ(activeItem.now, 1);
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    EXPECT_FALSE(Apply({
        Select(2), {"inventory.remove", {{"item", ITEM_TASTY_WATER}, {"quantity", 1}}, "test.quick"}
    }));
    EXPECT_EQ(itemNowSel, 1);
    EXPECT_EQ(std::memcmp(save.get(), before.get(), sizeof(CSaveData)), 0);
    auto command = Select(2);
    command.owner.clear();
    EXPECT_FALSE(Apply({command}));
}

TEST_F(ModQuickItems, RejectsStaleModelsAndUnsafeNativeContextsWithReasons) {
    auto reject = [&] {
        EXPECT_FALSE(Apply({Select(2)}));
        EXPECT_FALSE(error.empty());
        EXPECT_FALSE(mods::ReadQuickItems()["slots"][1]["ready"]);
        EXPECT_EQ(itemNowSel, 1);
    };
    activeItem.item[2] = ITEM_BREAD;
    reject();
    activeItem.item[2] = ITEM_TASTY_WATER;
    activeItem.model[2] = 16;
    reject();
    activeItem.model[2] = 1;
    pool->model_cash[1] = 6;
    reject();
    pool->model_cash[1] = 1;
    pool->cash_item[1] = ITEM_BREAD;
    reject();
    pool->cash_item[1] = ITEM_TASTY_WATER;
    pool->model[1] = ITEM_MODEL_THROWN;
    reject();
    pool->model[1] = ITEM_MODEL_LOADED;
    UserStatus->item_pack.quick_item_qty[1] = 0;
    reject();
    UserStatus->item_pack.quick_item_qty[1] = 1;
    BtActStatus.action_on = 3;
    reject();
    BtActStatus.action_on = 0;
    BtEventMode = 1;
    reject();
    BtEventMode = 0;
    gameTask = GAME_TASK_EYE_CAMERA;
    reject();
    gameTask = GAME_TASK_PLAY;
    mode = GAME_MODE_EDIT;
    reject();
    mode = GAME_MODE_DUNGEON;
    activeItem.models = nullptr;
    reject();
}

TEST_F(ModQuickItems, ExactSchemaAndNoSaveQueryAreSafe) {
    EXPECT_TRUE(mods::ValidateOperation("items.quick_slot_select", {
                                                                       {"slot", 3}
    })
                    .empty());
    for (nlohmann::json slot : {nlohmann::json(0), nlohmann::json(4), nlohmann::json(1.0), nlohmann::json(true)}) {
        EXPECT_FALSE(mods::ValidateOperation("items.quick_slot_select", {
                                                                            {"slot", slot}
        })
                         .empty());
    }
    EXPECT_FALSE(mods::ValidateOperation("items.quick_slots", {
                                                                  {"slot", 1}
    })
                     .empty());
    SaveData = nullptr;
    const auto state = mods::ReadQuickItems();
    EXPECT_FALSE(state["ready"]);
    EXPECT_TRUE(state["slots"].empty());
    EXPECT_FALSE(state.contains("selected"));
}
} // namespace
