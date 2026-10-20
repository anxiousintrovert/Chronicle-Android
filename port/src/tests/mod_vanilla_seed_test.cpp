#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#if __has_include("mods/runtime.hpp")
#include "mods/runtime.hpp"
#endif
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#ifndef DNG_GRID
#define DNG_GRID 20
#endif
#if __has_include("chronicle/dungeon_limits.hpp")
#include "chronicle/dungeon_limits.hpp"
constexpr int AuditStorageStride = chronicle::dungeon::GridStride;
#else
constexpr int AuditStorageStride = DNG_GRID;
#endif
TEST(ModVanillaAudit, SeedMatrix) {
    const char *out = std::getenv("AUDIT_OUTPUT");
    if (!out) {
        GTEST_SKIP() << "Set AUDIT_OUTPUT to run the cross-build seed audit";
    }
    std::ofstream output(out);
    ASSERT_TRUE(output.good());

#if __has_include("mods/runtime.hpp")
    mods::Manager manager;
    const char   *mod_root = std::getenv("AUDIT_MODS");
    manager.Scan(mod_root ? std::vector<std::filesystem::path>{mod_root} : std::vector<std::filesystem::path>{}, {});
    mods::Scripts().Start(manager);
    ASSERT_FALSE(mods::Scripts().HasGameplay());
    ASSERT_TRUE(manager.Overrides().empty());
#endif
    static CDngStatusData status;
    UserStatus = &status;
    for (int dungeon : {0, 1, 2, 4, 5, 6}) {
        for (int first : {0, 1}) {
            for (int rooms : {6, 4, 8, 12, 16}) {
                for (unsigned seed = 1; seed <= 64; ++seed) {
                    auto map = std::make_unique<CDungeonMap>();
                    std::memset(&status, 0, sizeof(status));
                    status.res_limit_zone_current = -1;
                    status.cur_floor = 10;
                    status.party_size = 6;
                    selectMapNo = dungeon;
                    NowDngMap = map.get();
                    map->map_seed = seed;
                    srand(seed);
#if __has_include("mods/runtime.hpp")
                    mods::Scripts().Tick({1, dungeon, 0, true}, [](int) { ADD_FAILURE()<<"unexpected native write"; return false; }, [](std::string_view) { ADD_FAILURE()<<"unexpected native read"; return nlohmann::json(); });
#endif
                    map->buildRandomMap(rooms, first);
                    int active_grid = DNG_GRID;
#if __has_include("chronicle/dungeon_build.hpp")
                    active_grid = map->GridExtent();
#endif
                    nlohmann::json j = {
                        {"input_seed",      seed              },
                        {"dungeon",         dungeon           },
                        {"first",           first             },
                        {"requested_rooms", rooms             },
                        {"map_seed",        map->map_seed     },
                        {"room_num",        map->room_num     },
                        {"gate_key",        map->gate_key_no  },
                        {"next_rng",        rand()            },
                        {"grid",            active_grid       },
                        {"map_bytes",       sizeof(*map)      },
                        {"road_part",       MAP_PARTS_URA_ROAD}
                    };
                    j["rooms"] = nlohmann::json::array();
                    for (int i = 0; i < map->room_num; ++i) {
                        auto &r = map->rooms[i];
                        j["rooms"].push_back({r.x, r.y, r.width, r.height});
                    }
                    j["cells"] = nlohmann::json::array();
                    // Export every occupied storage cell, including the unused
                    // border. An accidental extra cell must fail comparison.
                    for (int y = 0; y < AuditStorageStride; ++y) {
                        for (int x = 0; x < AuditStorageStride; ++x) {
                            auto &c = map->cells[x + y * AuditStorageStride];
                            if (c.parts_no != MAP_PARTS_NONE) {
                                j["cells"].push_back({x, y, c.parts_no, c.direction});
                            }
                        }
                    }
                    j["links"] = nlohmann::json::array();
                    for (const auto &link : map->room_link) {
                        if (link.used) {
                            j["links"].push_back({link.door_x, link.door_y, link.item_x, link.item_y});
                        }
                    }
                    j["boxes"] = nlohmann::json::array();
                    for (const auto &box : map->boxes) {
                        if (box.used) {
                            j["boxes"].push_back({box.item_no, box.pos[0], box.pos[2], box.kind, box.trap_no});
                        }
                    }
                    output << j.dump() << '\n';
                    output.flush();
                }
            }
        }
    }
    NowDngMap = nullptr;
    UserStatus = nullptr;
}
