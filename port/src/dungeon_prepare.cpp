#include "chronicle/dungeon_prepare.hpp"

#include "chronicle/dungeon_events.hpp"
#include "chronicle/random_state.hpp"
#include "dun/gameloop.hpp"
#include "monstorunit.hpp"
#include "savedata.hpp"

namespace chronicle::dungeon {
namespace {
struct RestoreContext {
    CUserStatus      *status = UserStatus;
    CDungeonMap      *map = NowDngMap;
    CDungeonEventMan *events = NowEventMan;
    std::uint32_t     random = chronicle::RandomState();

    std::vector<std::byte> scratch = CaptureDungeonBuildScratch();

    ~RestoreContext() {
        RestoreDungeonBuildScratch(scratch);
        UserStatus = status;
        NowDngMap = map;
        NowEventMan = events;
        chronicle::RestoreRandomState(random);
    }
};
} // namespace

PreparedMaps PrepareMaps(const CDungeonMap &model, const CDungeonEventMan &events,
                         const CDngStatusData &status, BuildLayout layout) {
    RestoreContext restore;
    BuildScope     scope(layout);
    if (!SaveData || SaveData->GetDngStatus() != &status || selectMapNo < 0 || selectMapNo >= 7 ||
        model.map_type != 1 || status.cur_floor < 0 || status.cur_floor >= maxFloorTbl__3[selectMapNo]) {
        throw std::runtime_error("custom floor preparation requires loaded procedural resources and current save");
    }
    PreparedMaps result;
    result.front = std::make_unique<CDungeonMap>(model);
    result.status = std::make_unique<CDngStatusData>(status);
    auto definitions = std::make_unique<CDungeonEventMan>(events);
    // Definitions reference loaded frames, which are read only during preflight.
    // Every runtime record starts free, as at the native regeneration boundary.
    for (auto &event : definitions->event) {
        event.event = nullptr;
        event.enabled = event.switch_on = false;
        event.hold = 0;
        event.chara_done = -1;
    }
    definitions->port_event_limit = RetailRuntimeEventCapacity;
    UserStatus = result.status.get();
    NowDngMap = result.front.get();
    NowEventMan = definitions.get();
    result.status->LostGateKey();
    result.front->buildRandomMap(6, 1);
    result.front->buildEventData(status.cur_floor, 1, 1);
    result.front->FlushCheckMask();
    PrepareExpandedDungeonEvents(*definitions, *result.front);
    result.back = std::make_unique<CDungeonMap>(*result.front);
    NowDngMap = result.back.get();
    result.back->buildRandomMap(6, 0);
    result.back->buildEventData(status.cur_floor, 1, 0);
    result.back->FlushCheckMask();
    PrepareExpandedDungeonEvents(*definitions, *result.back);
    result.status->ClearEventFlag();
    result.random_state = chronicle::RandomState();
    result.build_scratch = CaptureDungeonBuildScratch();
    return result;
}
} // namespace chronicle::dungeon
