#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <stdexcept>

#include "mods/api.hpp"
#include "mods/party.hpp"
#include "userstatus.hpp"

namespace {
using Json = nlohmann::json;

class ModParty : public testing::Test {
protected:
    CUserStatus                status{};
    mods::PartySwitchQueue     queue;
    std::string                error;
    std::array<std::string, 1> owners{"test.party"};
    int                        loaded = 0;

    void SetUp() override {
        status.party_size = 2;
        status.cur_chara = 0;
        status.res_limit_zone_current = -1;
        for (int i = 0; i < 2; ++i) {
            status.hp[i] = status.max_hp[i] = 100;
            status.ailments[i] = 0;
            status.equipped_weapon_slot[i] = 0;
        }
        status.chara_weapons[0][0].item_no = ITEM_WEAPON_DAGGER;
        status.chara_weapons[1][0].item_no = ITEM_WEAPON_WOODEN_SLINGSHOT;
    }

    mods::GameCommand Command(int target = 1) {
        return {"party.set_active", {{"character", target}}, owners[0]};
    }

    bool Queue(int target = 1, const mods::PartySwitchQueue::Preflight &preflight = {}) {
        return queue.Queue(&status, {}, 10, {Command(target)}, 0, preflight, error);
    }

    void Finish() {
        queue.Finish(&status, {}, 10, owners, {}, [this](int target) {++loaded;status.cur_chara=target; });
    }
};

TEST_F(ModParty, DormantAndQueuedRequestsPreserveSaveAndRngUntilFrameBoundary) {
    const auto before = status;
    srand(715);
    const int next = rand();
    srand(715);
    Finish();
    EXPECT_EQ(queue.State()["state"], "idle");
    ASSERT_TRUE(Queue()) << error;
    EXPECT_EQ(std::memcmp(&status, &before, sizeof(status)), 0);
    EXPECT_EQ(rand(), next);
    EXPECT_EQ(loaded, 0);
    EXPECT_EQ(queue.State()["state"], "pending");
    Finish();
    EXPECT_EQ(loaded, 1);
    EXPECT_EQ(status.cur_chara, 1);
    EXPECT_EQ(queue.State()["state"], "complete");
    Finish();
    EXPECT_EQ(loaded, 1);
}

TEST_F(ModParty, SameMemberCompletesWithoutLoadingOrAssetProbes) {
    ASSERT_TRUE(Queue(0, [](int) -> std::string { throw std::runtime_error("must not probe"); })) << error;
    Finish();
    EXPECT_EQ(loaded, 0);
    EXPECT_EQ(queue.State()["state"], "complete");
}

TEST_F(ModParty, RejectsMixedTransactionsCurrencyAndCompetingOwners) {
    auto other = Command();
    other.owner = "another.mod";
    EXPECT_FALSE(queue.Queue(&status, {}, 10, {Command(), other}, 0, {}, error));
    EXPECT_FALSE(queue.Queue(&status, {}, 10, {Command()}, 1, {}, error));
    EXPECT_FALSE(queue.Queue(&status, {}, 10, {}, 0, {}, error));
    auto no_owner = Command();
    no_owner.owner.clear();
    EXPECT_FALSE(queue.Queue(&status, {}, 10, {no_owner}, 0, {}, error));
    ASSERT_TRUE(Queue()) << error;
    EXPECT_FALSE(queue.Queue(&status, {}, 10, {other}, 0, {}, error));
    EXPECT_NE(error.find(owners[0]), std::string::npos);
    EXPECT_EQ(queue.State()["owner"], owners[0]);
}

TEST_F(ModParty, ValidatesExactSchemaAndNativePartyInvariants) {
    for (const auto &args : std::vector<Json>{
             Json::array(), {{"character", 1.0}},
              {{"character", -1}},
              {{"character", 6}},
              {{"character", 1}, {"extra", true}}
    }) {
        EXPECT_FALSE(mods::ValidateOperation("party.set_active", args).empty());
    }
    EXPECT_TRUE(mods::ValidateOperation("party.switch_state", Json::object()).empty());
    EXPECT_FALSE(mods::ValidateOperation("party.switch_state", {
                                                                   {"extra", 1}
    })
                     .empty());
    EXPECT_FALSE(Queue(2));
    status.party_size = 7;
    EXPECT_FALSE(Queue());
    status.party_size = 2;
    status.cur_chara = 2;
    EXPECT_FALSE(Queue());
    status.cur_chara = 0;
    status.hp[0] = 0;
    EXPECT_FALSE(Queue());
    status.hp[0] = 100;
    status.hp[1] = 0;
    EXPECT_FALSE(Queue());
    status.hp[1] = 100;
    status.ailments[1] = AILMENT_KNOCKED_OUT;
    EXPECT_FALSE(Queue());
    status.ailments[1] = 0;
    status.res_limit_zone_current = 1;
    EXPECT_FALSE(Queue());
    status.res_limit_zone_current = -1;
    status.equipped_weapon_slot[1] = 11;
    EXPECT_FALSE(Queue());
    status.equipped_weapon_slot[1] = 0;
    status.chara_weapons[1][0].item_no = ITEM_WEAPON_DAGGER;
    EXPECT_FALSE(Queue());
    EXPECT_FALSE(queue.Pending());
    EXPECT_EQ(status.cur_chara, 0);
}

TEST_F(ModParty, CancelsRequestsWhenOwnerStopsOrRuntimeRestarts) {
    ASSERT_TRUE(Queue());
    queue.Finish(&status, {}, 10, {}, {}, [this](int) { ++loaded; });
    EXPECT_EQ(queue.State()["state"], "cancelled");
    ASSERT_TRUE(Queue());
    queue.Finish(&status, {}, 11, owners, {}, [this](int) { ++loaded; });
    EXPECT_EQ(queue.State()["state"], "cancelled");
    EXPECT_EQ(loaded, 0);
    EXPECT_EQ(status.cur_chara, 0);
}

TEST_F(ModParty, RechecksStatusIdentityNativeChangesAndActionGuards) {
    ASSERT_TRUE(Queue());
    auto replacement = status;
    queue.Finish(&replacement, {}, 10, owners, {}, [this](int) { ++loaded; });
    EXPECT_EQ(queue.State()["state"], "failed");
    ASSERT_TRUE(Queue());
    status.cur_chara = 1;
    Finish();
    EXPECT_EQ(queue.State()["state"], "failed");
    status.cur_chara = 0;
    ASSERT_TRUE(Queue());
    queue.Finish(&status, "menu opened", 10, owners, {}, [this](int) { ++loaded; });
    EXPECT_EQ(queue.State()["reason"], "menu opened");
    ASSERT_TRUE(Queue());
    status.hp[1] = 0;
    Finish();
    EXPECT_EQ(queue.State()["state"], "failed");
    EXPECT_EQ(loaded, 0);
}

TEST_F(ModParty, MissingResourcesRejectBeforeAndAfterPublication) {
    EXPECT_FALSE(Queue(1, [](int) { return "missing character model"; }));
    EXPECT_FALSE(queue.Pending());
    ASSERT_TRUE(Queue());
    queue.Finish(&status, {}, 10, owners, [](int) { return "resource removed"; }, [this](int) { ++loaded; });
    EXPECT_EQ(queue.State()["reason"], "resource removed");
    EXPECT_EQ(loaded, 0);
    EXPECT_EQ(status.cur_chara, 0);
}

TEST_F(ModParty, LoaderFailureAndExceptionsAreReportedWithoutRetrying) {
    ASSERT_TRUE(Queue());
    queue.Finish(&status, {}, 10, owners, {}, [](int) {});
    EXPECT_EQ(queue.State()["state"], "failed");
    ASSERT_TRUE(Queue());
    queue.Finish(&status, {}, 10, owners, {}, [](int) { throw std::runtime_error("loader failure"); });
    EXPECT_EQ(queue.State()["state"], "failed");
    EXPECT_NE(queue.State()["reason"].get<std::string>().find("loader failure"), std::string::npos);
    EXPECT_FALSE(queue.Pending());
    Finish();
    EXPECT_EQ(loaded, 0);
}
} // namespace
