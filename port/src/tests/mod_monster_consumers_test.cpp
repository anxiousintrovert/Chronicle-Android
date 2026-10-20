#include <gtest/gtest.h>

#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "frame.hpp"
#include "monstorunit.hpp"
#include "runscript.hpp"
#include "runscript_opcodes.hpp"

extern CFrame *CharaFrame;
extern int lockOnTargetNo;

namespace {
struct Context {
    CMonstorUnit *unit = NowMonstorUnit;
    CUserStatus *status = UserStatus;
    CFrame *frame = CharaFrame;
    int target = lockOnTargetNo;
    ~Context() {
        NowMonstorUnit = unit;
        UserStatus = status;
        CharaFrame = frame;
        lockOnTargetNo = target;
    }
};
}

TEST(ModMonsterConsumers, LockOnIncludesLastActiveSlotButIgnoresDormantStorage) {
    Context restore;
    auto unit = std::make_unique<CMonstorUnit>();
    auto status = std::make_unique<CDngStatusData>();
    auto frame = std::make_unique<CFrame>();
    NowMonstorUnit = unit.get();
    UserStatus = status.get();
    CharaFrame = frame.get();
    status->cur_chara = 0;
    frame->SetPosition(0, 0, 0);
    frame->SetRotation(0, 0, 0);
    unit->port_slot_limit = 256;
    unit->CleanViewMonstor(0);
    auto &actor = unit->monster[255];
    actor.state = 2;
    actor.hp = 10;
    actor.revealed = 1;
    actor.lockon_enabled = true;
    actor.lock_range = 100;
    unit->chara[255][0].SetPosition(0, 0, 10);
    // Already selected: native selection returns before drawing any UI.
    lockOnTargetNo = 255;
    EXPECT_EQ(SetNearLockOnTarget(0, 0), 1);
    EXPECT_EQ(lockOnTargetNo, 255);
    unit->port_slot_limit = 16;
    EXPECT_EQ(SetNearLockOnTarget(0, 0), 0);
    EXPECT_EQ(lockOnTargetNo, -1);
}

TEST(ModMonsterConsumers, NativeNearbyMonsterOpcodeFindsHighSlots) {
    Context restore;
    auto unit = std::make_unique<CMonstorUnit>();
    NowMonstorUnit = unit.get();
    unit->port_slot_limit = 256;
    unit->CleanViewMonstor(0);
    unit->current_monster = 0;
    unit->chara[0][0].SetPosition(0, 0, 0);
    unit->monster[255].state = 2;
    unit->chara[255][0].SetPosition(5, 6, 7);
    RS_STACKDATA values[4]{}, args[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        args[i].type = RS_PTR;
        args[i].p = &values[i];
    }
    _GET_NEAR_MONSTER(args, 4);
    EXPECT_EQ(values[3].i, 255);
    EXPECT_FLOAT_EQ(values[0].f, 5);
    EXPECT_FLOAT_EQ(values[1].f, 6);
    EXPECT_FLOAT_EQ(values[2].f, 7);
    // A low candidate avoids the native no-candidate uninitialized coordinates.
    unit->monster[1].state = 2;
    unit->chara[1][0].SetPosition(20, 0, 0);
    unit->port_slot_limit = 16;
    _GET_NEAR_MONSTER(args, 4);
    EXPECT_EQ(values[3].i, 1);
}

TEST(ModMonsterConsumers, ScriptVariablesForLastSlotDoNotAliasRetailSlots) {
    Context restore;
    auto unit = std::make_unique<CMonstorUnit>();
    NowMonstorUnit = unit.get();
    unit->port_slot_limit = 256;
    PUSH_INT_DATA[0][7] = 91;
    unit->current_monster = 255;
    RS_STACKDATA args[2]{}, value{};
    args[0].type = RS_INT;
    args[0].i = 7;
    args[1].type = RS_INT;
    args[1].i = 12345;
    EXPECT_EQ(_PUSH_IGLOBAL(args, 2), 1);
    args[1].type = RS_PTR;
    args[1].p = &value;
    EXPECT_EQ(_POP_IGLOBAL(args, 2), 1);
    EXPECT_EQ(value.i, 12345);
    EXPECT_EQ(PUSH_INT_DATA[0][7], 91);
    args[0].i = 8;
    EXPECT_EQ(_PUSH_IGLOBAL(args, 2), 0);
    EXPECT_EQ(_POP_IGLOBAL(args, 2), 0);
}
