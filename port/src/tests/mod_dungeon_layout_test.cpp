#include <gtest/gtest.h>

#include <map>

#include "chronicle/random_state.hpp"
#include "mods/api.hpp"
#include "mods/dungeon_layout.hpp"
#include "mods/entities.hpp"
#include "savedata.hpp"

namespace {
mods::GameCommand Layout(std::string owner = "test.a", int dungeon = 0) {
    return {
        "dungeon.layout_set", {{"dungeon", dungeon}, {"rooms", 64}, {"area", 46}, {"room_min", 3}, {"room_max", 6}},
         owner
    };
}

class ModDungeonLayout : public testing::Test {
protected:
    void SetUp() override { mods::RetainOwnedLayouts({}); }

    void TearDown() override { mods::RetainOwnedLayouts({}); }
};

TEST_F(ModDungeonLayout, OwnershipConflictResetAndAtomicBatch) {
    auto        save = std::make_unique<CSaveData>();
    std::string error;
    auto        apply = [&](std::vector<mods::GameCommand> commands) { return mods::ApplyEntityAndSave(*save, nullptr, commands, 0, error, nullptr, nullptr); };
    ASSERT_TRUE(apply({Layout()})) << error;
    EXPECT_EQ(mods::ReadLayout({
                  {"dungeon", 0}
    })["owner"],
              "test.a");
    EXPECT_FALSE(apply({Layout("test.b", 1), Layout("test.b")}));
    EXPECT_NE(error.find("test.a"), std::string::npos);
    EXPECT_FALSE(mods::ReadLayout({
        {"dungeon", 1}
    })["enabled"]
                     .get<bool>());
    EXPECT_FALSE(apply({
        {"dungeon.layout_reset", {{"dungeon", 0}}, "test.b"}
    }));
    ASSERT_TRUE(apply({
        {"dungeon.layout_reset", {{"dungeon", 0}}, "test.a"}
    }));
    EXPECT_FALSE(mods::HasOwnedLayouts());
}

TEST_F(ModDungeonLayout, SaveFailureAndAbandonedStagePublishNothing) {
    auto        save = std::make_unique<CSaveData>();
    std::string error;
    const auto  rng = chronicle::RandomState();
    {
        mods::LayoutTransaction tx;
        ASSERT_TRUE(tx.Stage({Layout()}, error));
    }
    EXPECT_FALSE(mods::HasOwnedLayouts());
    EXPECT_FALSE(mods::ApplyEntityAndSave(*save, nullptr, {
                                                              Layout(), {"economy.spend", {{"amount", 65535}}, "test.a"}
    },
                                          0, error, nullptr, nullptr));
    EXPECT_FALSE(mods::HasOwnedLayouts());
    EXPECT_EQ(chronicle::RandomState(), rng);
    EXPECT_FALSE(mods::BuildOwnedDungeonLayout());
    EXPECT_EQ(chronicle::RandomState(), rng);
}

TEST_F(ModDungeonLayout, ValidationAndOwnerRetirement) {
    auto command = Layout();
    EXPECT_TRUE(mods::ValidateOperation(command.operation, command.arguments).empty());
    for (const auto &[field, value] : std::map<std::string, int>{
             {"rooms",    65},
             {"area",     47},
             {"room_min", 2 },
             {"room_max", 7 }
    }) {
        auto a = command.arguments;
        a[field] = value;
        EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    }
    auto a = command.arguments;
    for (int count : {1, 25}) {
        a["chests"] = count;
        EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    }
    a["chests"] = 12;
    a["monsters"] = 16;
    EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    a["monsters"] = 0;
    EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    a["monsters"] = 1;
    EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    a["monsters"] = 2;
    EXPECT_TRUE(mods::ValidateOperation(command.operation, a).empty());
    a["dungeon"] = 1;
    EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    a["monsters"] = 3;
    EXPECT_TRUE(mods::ValidateOperation(command.operation, a).empty());
    a["dungeon"] = 0;
    a["monsters"] = 5;
    EXPECT_TRUE(mods::ValidateOperation(command.operation, a).empty());
    command.arguments = a;
    a["room_min"] = 6;
    a["room_max"] = 3;
    EXPECT_FALSE(mods::ValidateOperation(command.operation, a).empty());
    mods::LayoutTransaction tx;
    std::string             error;
    ASSERT_TRUE(tx.Stage({command}, error));
    tx.Commit();
    EXPECT_TRUE(mods::HasOwnedLayouts());
    EXPECT_EQ(mods::ReadLayout({{"dungeon", 0}})["requested"]["chests"], 12);
    EXPECT_EQ(mods::ReadLayout({{"dungeon", 0}})["requested"]["monsters"], 5);
    mods::RetainOwnedLayouts({});
    EXPECT_FALSE(mods::HasOwnedLayouts());
}
} // namespace
