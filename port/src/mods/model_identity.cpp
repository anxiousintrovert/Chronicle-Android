#include "model_identity.hpp"
#include "monstorunit.hpp"
#include <array>
#include <iterator>

namespace mods {
namespace {
struct Model { int native_id = -1; const void *script = nullptr, *frame = nullptr; };
struct Unit { const CMonstorUnit *unit = nullptr; std::array<Model, 9> models; };
constinit std::array<Unit, 8> identities{};
Unit *Find(const CMonstorUnit *unit) noexcept {
    if (!unit) return nullptr;
    for (auto &entry : identities) if (entry.unit == unit) return &entry;
    return nullptr;
}
}
void ClearNativeModelIdentities(CMonstorUnit *unit) noexcept {
    if (auto *entry = Find(unit)) *entry = {};
}
void BeginNativeModelLoad(CMonstorUnit *unit, int slot) noexcept {
    if (!unit) return;
    if (unit->model_count <= 0 || unit->model_count > 9) { ClearNativeModelIdentities(unit); return; }
    if (slot < 0 || slot >= 9) return;
    if (auto *entry = Find(unit)) entry->models[slot] = {};
}
void RecordNativeModelLoad(CMonstorUnit *unit, int slot, int native_id) noexcept {
    if (!unit || unit->model_count < 1 || unit->model_count > 9 || slot < 0 || slot >= unit->model_count ||
        native_id < 0 || native_id >= static_cast<int>(std::size(MonstorTable)) ||
        !unit->script_data[slot] || !unit->base_chara[slot][0].frame) return;
    auto *entry = Find(unit);
    if (!entry) for (auto &candidate : identities) if (!candidate.unit) { entry = &candidate; entry->unit = unit; break; }
    if (!entry) return; // No eviction of another unit's valid identities.
    entry->models[slot] = {native_id, unit->script_data[slot], unit->base_chara[slot][0].frame};
}
int NativeMonsterModelId(const CMonstorUnit *unit, int slot) noexcept {
    if (!unit || unit->model_count < 1 || unit->model_count > 9 || slot < 0 || slot >= unit->model_count) return -1;
    const auto *entry = Find(unit); if (!entry) return -1;
    const auto &model = entry->models[slot];
    if (model.native_id < 0 || model.script != unit->script_data[slot] || model.frame != unit->base_chara[slot][0].frame) return -1;
    return model.native_id;
}
int NativeActorModelId(const CMonstorUnit *unit, unsigned slot) noexcept {
    if (!unit || slot >= unit->SlotLimit() || unit->monster[slot].state == -1) return -1;
    return NativeMonsterModelId(unit, unit->monster[slot].base_model);
}
}
