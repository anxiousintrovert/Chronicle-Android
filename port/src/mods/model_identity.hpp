#pragma once
class CMonstorUnit;
namespace mods {
// Loader notifications, never inference from names/stats. Storage is bounded,
// framework-owned and allocation-free. Unknown/unobserved identities return -1.
void BeginNativeModelLoad(CMonstorUnit *unit, int slot) noexcept;
void RecordNativeModelLoad(CMonstorUnit *unit, int slot, int native_id) noexcept;
void ClearNativeModelIdentities(CMonstorUnit *unit) noexcept;
int NativeMonsterModelId(const CMonstorUnit *unit, int loaded_slot) noexcept;
int NativeActorModelId(const CMonstorUnit *unit, unsigned actor_slot) noexcept;
}
