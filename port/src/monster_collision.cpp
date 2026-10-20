#include "chronicle/monster_collision.hpp"

#include <algorithm>
#include <stdexcept>
#include <typeinfo>
#include <vector>

#include "frame.hpp"
#include "monstorunit.hpp"

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
