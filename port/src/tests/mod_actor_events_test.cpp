#include <gtest/gtest.h>
#include "mods/actor_events.hpp"
#include <string>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<mods::ActorEvent>);
TEST(ModActorEvents, DormantCaptureAndDisableDiscardEverything) {
    mods::ActorEventQueue queue;mods::ActorEvent event;event.entity=42;
    EXPECT_FALSE(queue.Push(event));EXPECT_TRUE(queue.Pending().empty());EXPECT_EQ(queue.Dropped(),0u);
    queue.Enable(true);ASSERT_TRUE(queue.Push(event));queue.Enable(false);
    EXPECT_TRUE(queue.Pending().empty());EXPECT_EQ(queue.Dropped(),0u);
    queue.Enable(true);EXPECT_TRUE(queue.Pending().empty());
}
TEST(ModActorEvents, CaptureCopiesValuesAndLabelsWithoutBorrowingProducerStorage) {
    mods::ActorEventQueue queue;queue.Enable(true);mods::ActorEvent event;
    event.entity=42;event.epoch=7;event.hp=12;event.max_hp=20;event.position={1,2,3};
    std::string owner="author.package",definition="actor";
    ASSERT_TRUE(queue.Push(event,owner,definition));event.hp=0;event.position[0]=99;owner="changed";definition="changed";
    const auto &captured=queue.Pending()[0];EXPECT_EQ(captured.entity,42u);EXPECT_EQ(captured.epoch,7u);
    EXPECT_EQ(captured.hp,12);EXPECT_EQ(captured.position[0],1);
    EXPECT_STREQ(captured.owner.data(),"author.package");EXPECT_STREQ(captured.definition.data(),"actor");
}
TEST(ModActorEvents, OverflowAndInvalidLabelsNeverPublishPartialEvents) {
    mods::ActorEventQueue queue;queue.Enable(true);mods::ActorEvent event;
    EXPECT_FALSE(queue.Push(event,std::string(129,'x')));
    EXPECT_FALSE(queue.Push(event,{},std::string("bad\0id",6)));EXPECT_EQ(queue.Dropped(),2u);EXPECT_TRUE(queue.Pending().empty());
    for(unsigned i=0;i<64;++i){event.entity=i+1;ASSERT_TRUE(queue.Push(event,std::string(128,'a')));}
    EXPECT_FALSE(queue.Push(event));EXPECT_EQ(queue.Dropped(),3u);ASSERT_EQ(queue.Pending().size(),64u);
    for(unsigned i=0;i<64;++i)EXPECT_EQ(queue.Pending()[i].entity,i+1);
    queue.Reset();EXPECT_TRUE(queue.Enabled());EXPECT_TRUE(queue.Pending().empty());EXPECT_EQ(queue.Dropped(),0u);
}
TEST(ModActorEvents, UnitCancellationPreservesOtherUnitsAndSameSlotGenerations) {
    mods::ActorEventQueue queue;queue.Enable(true);char first{},second{};
    auto *a=reinterpret_cast<CMonstorUnit *>(&first);auto *b=reinterpret_cast<CMonstorUnit *>(&second);
    mods::ActorEvent event;event.unit=a;event.entity=256;ASSERT_TRUE(queue.Push(event));
    event.unit=b;event.entity=512;ASSERT_TRUE(queue.Push(event));
    event.kind=mods::ActorEventKind::Removed;ASSERT_TRUE(queue.Push(event));
    event.kind=mods::ActorEventKind::Spawned;event.entity=768;ASSERT_TRUE(queue.Push(event));
    queue.CancelUnit(a);ASSERT_EQ(queue.Pending().size(),3u);
    EXPECT_EQ(queue.Pending()[0].entity,512u);EXPECT_EQ(queue.Pending()[1].kind,mods::ActorEventKind::Removed);
    EXPECT_EQ(queue.Pending()[2].entity,768u);queue.CancelUnit(b);EXPECT_TRUE(queue.Pending().empty());
}
