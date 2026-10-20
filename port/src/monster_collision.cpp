#include "chronicle/monster_collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <typeinfo>
#include <vector>

#include "frame.hpp"
#include "monstorunit.hpp"
#include "dungeonmap.hpp"
#include "gameutil.hpp"
#include "mathconst.h"
#include "mathutil.hpp"

namespace chronicle::monsters {
namespace {
constexpr unsigned MaxPolys = 65536;

unsigned Bound(CFrame *frame, unsigned &visits, unsigned depth = 0) {
    if (!frame) return 0;
    if (++visits > 4096 || depth > 64) throw std::runtime_error("monster collision frame graph exceeds budget");
    if (frame->flags == 4) return 0;
    unsigned count = 0;
    if (frame->collision && (frame->flags & 1)) {
        auto *mesh = dynamic_cast<CCollisionMDT *>(frame->collision);
        if (!mesh || typeid(*mesh) != typeid(CCollisionMDT) || mesh->mesh_count < 0 ||
            mesh->mesh_count > int(MaxPolys) || (mesh->mesh_count && !mesh->mesh)) {
            throw std::runtime_error("unsupported monster collision geometry");
        }
        count = unsigned(mesh->mesh_count);
    }
    if (!(frame->flags & 2) && !(frame->flags & 4)) {
        for (auto *child = frame->child; child; child = child->brother) {
            const auto n = Bound(child, visits, depth + 1);
            if (n > MaxPolys - count) throw std::runtime_error("monster collision geometry exceeds budget");
            count += n;
        }
    }
    return count;
}
}

std::span<CCPoly> ExpandedTerrainCollision(CDungeonMap &map, const float *position, const float *player_position) {
    if (map.map_type != 1 || !position || !player_position || map.atra_num < 0 || map.atra_num > 8) {
        throw std::invalid_argument("expanded monster terrain requires a valid procedural floor");
    }
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(position[i]) || !std::isfinite(player_position[i]) ||
            std::abs(position[i] / 160.0f) >= float(std::numeric_limits<int>::max() / 2)) {
            throw std::invalid_argument("invalid expanded monster collision position");
        }
    }
    struct Part {
        CFrame *frame;
        std::array<float, 3> position;
        float rotation;
        unsigned bound;
        bool rotate;
    };
    std::array<Part, 9 + 24 + 8> parts{};
    unsigned size = 0, total = 0, visits = 0;
    auto add = [&](CFrame *frame, const float *pos, bool rotate, float rotation) {
        if (!frame) throw std::runtime_error("missing expanded monster terrain collision frame");
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(pos[i])) throw std::runtime_error("invalid terrain collision position");
        }
        if (!std::isfinite(rotation)) throw std::runtime_error("invalid terrain collision rotation");
        const auto bound = Bound(frame, visits);
        if (size == parts.size() || bound > MaxPolys - total) {
            throw std::runtime_error("expanded monster terrain exceeds polygon budget");
        }
        parts[size++] = {frame, {pos[0], pos[1], pos[2]}, rotation, bound, rotate};
        total += bound;
    };
    constexpr int around[9][2] = {{0, 0}, {-1, 0}, {0, -1}, {1, 0}, {0, 1},
                                  {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (auto &offset : around) {
        const int x = int(position[0] / 160.0f) + offset[0];
        const int z = int(position[2] / 160.0f) + offset[1];
        if (x < 0 || z < 0 || x >= map.GridExtent() || z >= map.GridExtent()) continue;
        auto &cell = map.cells[x + z * chronicle::dungeon::GridStride];
        if (cell.parts_no == -1) continue;
        if (cell.parts_no < 0 || cell.parts_no >= 72) throw std::runtime_error("invalid terrain part index");
        auto &part = map.parts[cell.parts_no];
        if (!part.collision) continue;
        float turn = float(cell.direction) + float(part.collision_turn);
        if (turn > 3.0f) turn -= 3.0f;
        if (turn == 3.0f) turn = -1.0f;
        // Match native PI and operation order exactly.
        const float angle = (PI * (-90.0f * turn)) / 180.0f;
        const float pos[3] = {160.0f * float(x), 0, 160.0f * float(z)};
        add(part.collision, pos, true, angle);
    }
    for (auto &chest : map.boxes) {
        if (chest.used) add(map.box_collision_model, chest.pos, false, 0);
    }
    if (map.collision_model) {
        for (int i = 0; i < map.atra_num; ++i) {
            if (map.atra[i].used && DistVector(map.atra[i].pos, const_cast<float *>(player_position)) <= 400.0f) {
                add(map.collision_model, map.atra[i].pos, false, 0);
            }
        }
    }
    CBoxVu0 box{};
    for (int i = 0; i < 3; ++i) {
        const float extent = i == 1 ? 150.0f : 30.0f;
        box.min[i] = position[i] - extent;
        box.max[i] = position[i] + extent;
    }
    static std::vector<CCPoly> polys;
    polys.resize(total);
    unsigned count = 0;
    for (unsigned i = 0; i < size; ++i) {
        auto &part = parts[i];
        if (part.rotate) part.frame->SetRotation(0, part.rotation, 0);
        part.frame->SetPosition(part.position[0], part.position[1], part.position[2]);
        if (!part.bound) continue;
        const auto n = part.frame->PickUpNearPoly(polys.data() + count, box);
        if (n < 0 || unsigned(n) > part.bound) throw std::runtime_error("terrain collision writer violated its bound");
        count += unsigned(n);
    }
    polys.resize(count);
    return polys;
}

std::span<CCPoly> ExpandedActorCollision(CMonstorUnit &unit, std::span<const CCPoly> terrain, const CBoxVu0 &box) {
    if (unit.SlotLimit() <= RetailSlots || unit.current_monster < 0 ||
        unsigned(unit.current_monster) >= unit.SlotLimit() || terrain.size() > MaxPolys) {
        throw std::invalid_argument("expanded monster collision requires a valid expanded unit");
    }
    unsigned actors = 0;
    for (unsigned i = 0; i < unit.SlotLimit(); ++i) {
        if (i != unsigned(unit.current_monster) && unit.monster[i].state == 2) ++actors;
    }
    if (actors && !unit.collision) throw std::runtime_error("missing monster collision frame");
    unsigned visits = 0;
    const auto bound = actors ? Bound(unit.collision, visits) : 0;
    if (actors && bound > (MaxPolys - terrain.size()) / actors) {
        throw std::runtime_error("expanded monster collision exceeds polygon budget");
    }
    // Construct/allocate only after an expanded unit explicitly requests it.
    static std::vector<CCPoly> polys;
    polys.resize(terrain.size() + actors * bound);
    std::copy(terrain.begin(), terrain.end(), polys.begin());
    auto count = terrain.size();
    if (bound) {
        for (unsigned i = 0; i < unit.SlotLimit(); ++i) {
            if (i == unsigned(unit.current_monster) || unit.monster[i].state != 2) continue;
            sceVu0FVECTOR position;
            unit.chara[i][0].GetPosition(position);
            unit.collision->SetPosition(position);
            const auto scale = 2.0f * (0.1f * unit.monster[i].collision_radius);
            unit.collision->SetScale(scale, scale, scale);
            const auto n = unit.collision->PickUpNearPoly(polys.data() + count, box);
            if (n < 0 || unsigned(n) > bound) throw std::runtime_error("monster collision writer violated its bound");
            count += unsigned(n);
        }
    }
    polys.resize(count);
    return polys;
}
}
