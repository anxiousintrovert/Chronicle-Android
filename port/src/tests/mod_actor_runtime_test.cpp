#include <gtest/gtest.h>
#include "mods/actor_runtime.hpp"
#include "mods/api.hpp"
#include "mods/entity_tracking.hpp"
#include "mods/owned_actors.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "monstorunit.hpp"
#include "main.hpp"
#include "savedata.hpp"
#include "frame.hpp"
#include <memory>
#include <limits>
extern s32 mode, gameTask, NextMapNo, existFlag;
extern int load_snd_set, load_voice_set;

TEST(ModActorRuntime, StrictSchemasAndOwnershipCannotBeSuppliedInArguments) {
    using Json = nlohmann::json;
    Json spawn = {{"id", "guard"}, {"model", 0}, {"position", {1, 2, 3}}};
    EXPECT_TRUE(mods::ValidateOperation("entities.spawn", spawn).empty());
    EXPECT_TRUE(mods::ValidateOperation("entities.spawn_check", {{"model", 8}, {"position", {0, 0, 0}}}).empty());
    EXPECT_TRUE(mods::ValidateOperation("entities.owned", Json::object()).empty());
    EXPECT_TRUE(mods::ValidateOperation("entities.despawn", {{"entity", "256"}}).empty());
    for (Json id : {Json(""), Json("bad/id"), Json(std::string(65, 'a')), Json(1)}) {
        auto a = spawn; a["id"] = id; EXPECT_FALSE(mods::ValidateOperation("entities.spawn", a).empty());
    }
    for (Json model : {Json(-1), Json(9), Json(1.5), Json(true)}) {
        auto a = spawn; a["model"] = model; EXPECT_FALSE(mods::ValidateOperation("entities.spawn", a).empty());
    }
    auto a = spawn; a["owner"] = "someone.else"; EXPECT_FALSE(mods::ValidateOperation("entities.spawn", a).empty());
    a = spawn; a["position"] = {0, std::numeric_limits<double>::infinity(), 0};
    EXPECT_FALSE(mods::ValidateOperation("entities.spawn", a).empty());
    for (const char *handle : {"0", "0256", "-1", "1junk", "18446744073709551616"})
        EXPECT_FALSE(mods::ValidateOperation("entities.despawn", {{"entity", handle}}).empty());
    std::string error;
    EXPECT_FALSE(mods::ApplyActorCommands({{"entities.spawn", spawn, ""}}, 0, error));
    EXPECT_FALSE(mods::ApplyActorCommands({{"entities.spawn", spawn, "alpha"}}, 1, error));
    EXPECT_FALSE(mods::ApplyActorCommands({{"entities.spawn", spawn, "alpha"}, {"entities.spawn", spawn, "alpha"}}, 0, error));
}

namespace {
class ModActorRuntimeScene : public testing::Test {
protected:
    int old_mode=mode, old_task=gameTask, old_next=NextMapNo, old_exist=existFlag;
    int old_game=BtGameModeFlag, old_event=BtEventMode, old_draw=BtAllDrawFlag, old_map=selectMapNo;
    int old_sound=load_snd_set, old_voice=load_voice_set;
    CSaveData *old_save=SaveData; CUserStatus *old_user=UserStatus;
    CMonstorUnit *old_unit=NowMonstorUnit; CDungeonMap *old_dungeon=NowDngMap;
    CFrame *old_frame=CharaMain.frame;
    CCollisionData *old_collision=NowColData; CSHOT_EFFECT_PACK *old_shots=NowShotEffect;
    std::unique_ptr<CSaveData> save=std::make_unique<CSaveData>();
    std::unique_ptr<CMonstorUnit> unit=std::make_unique<CMonstorUnit>();
    std::unique_ptr<CDungeonMap> map=std::make_unique<CDungeonMap>(); CFrame frame;
    void SetUp() override {
        mods::StopTrackingEntities(); unit->CleanViewMonstor(0);
        mode=GAME_MODE_DUNGEON; gameTask=GAME_TASK_PLAY; BtGameModeFlag=BT_GAME_MODE_PLAY;
        BtEventMode=0; BtAllDrawFlag=1; NextMapNo=-1; existFlag=0; selectMapNo=0;
        load_snd_set=load_voice_set=-1;
        SaveData=save.get(); UserStatus=save->GetDngStatus(); UserStatus->cur_georama=0;
        NowMonstorUnit=unit.get(); NowDngMap=map.get(); CharaMain.frame=&frame;
        NowColData=nullptr; NowShotEffect=nullptr;
    }
    void TearDown() override {
        unit->CleanViewMonstor(0); mods::StopTrackingEntities();
        mode=old_mode; gameTask=old_task; NextMapNo=old_next; existFlag=old_exist;
        BtGameModeFlag=old_game; BtEventMode=old_event; BtAllDrawFlag=old_draw; selectMapNo=old_map;
        load_snd_set=old_sound; load_voice_set=old_voice;
        SaveData=old_save; UserStatus=old_user; NowMonstorUnit=old_unit; NowDngMap=old_dungeon;
        CharaMain.frame=old_frame; NowColData=old_collision; NowShotEffect=old_shots;
    }
    mods::EntityHandles::Handle Own(std::uint64_t epoch) {
        auto &actor=unit->monster[0]; actor.state=2; actor.hp=10; actor.kind=0; actor.attachment_kind=0;
        unit->alive_count=1; auto &handles=mods::TrackEntities(*unit);
        auto &owners=mods::BindActorOwnership(*unit, epoch); std::string error;
        EXPECT_TRUE(owners.Claim(handles, handles.Get(0), "test.inactive", "guard", error));
        return handles.Get(0);
    }
};
}
TEST_F(ModActorRuntimeScene, DespawnRequiresOwnerAndReportsPendingCleanupUntilRetrySucceeds) {
    const auto handle=Own(mods::Scripts().Epoch()); std::string error;
    EXPECT_FALSE(mods::ApplyActorCommands({{"entities.despawn", {{"entity", std::to_string(handle)}}, "other"}}, 0, error));
    EXPECT_EQ(unit->alive_count, 1); EXPECT_EQ(unit->monster[0].hp, 10);
    unit->monster[0].event_flag2=7;
    mods::RetainGameActors();
    auto state=mods::ReadActorOperation("entities.owned", nlohmann::json::object());
    ASSERT_EQ(state["actors"].size(), 1); EXPECT_FALSE(state["actors"][0]["cleanup_reason"].get<std::string>().empty());
    EXPECT_EQ(state["actors"][0]["owner"], "test.inactive");
    mods::StopTrackingEntities(); EXPECT_NE(mods::TrackedEntities(unit.get()), nullptr);
    unit->monster[0].event_flag2=-1;
    mods::RetainGameActors();
    EXPECT_EQ(unit->alive_count, 0); EXPECT_EQ(unit->monster[0].state, -1);
    EXPECT_TRUE(mods::ReadActorOperation("entities.owned", nlohmann::json::object())["actors"].empty());
    mods::StopTrackingEntities(); EXPECT_EQ(mods::TrackedEntities(unit.get()), nullptr);
}
TEST_F(ModActorRuntimeScene, PreviousEpochCannotBeReboundAndCleanupWaitsForStableNativeContext) {
    Own(mods::Scripts().Epoch()+1);
    EXPECT_THROW(mods::BindActorOwnership(*unit, mods::Scripts().Epoch()), std::runtime_error);
    BtEventMode=1; mods::RetainGameActors(); EXPECT_EQ(unit->alive_count, 1);
    BtEventMode=0; load_snd_set=1; mods::RetainGameActors(); EXPECT_EQ(unit->alive_count, 1);
    load_snd_set=-1; mods::RetainGameActors(); EXPECT_EQ(unit->alive_count, 0);
    EXPECT_FALSE(mods::HasBoundActorOwnership(unit.get()));
}
TEST_F(ModActorRuntimeScene, UnboundReadinessAndCleanupAreDormant) {
    unit->model_count=0;
    mods::RetainGameActors();
    auto check=mods::ReadActorOperation("entities.spawn_check", {{"model",0},{"position",{0,0,0}}});
    EXPECT_FALSE(check["ready"]); EXPECT_FALSE(check["reason"].get<std::string>().empty());
    EXPECT_EQ(mods::TrackedEntities(unit.get()), nullptr);
    EXPECT_TRUE(mods::ReadActorOperation("entities.owned", nlohmann::json::object())["actors"].empty());
}
