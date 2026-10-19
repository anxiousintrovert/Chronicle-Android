#pragma once
class CMonstorUnit;

namespace mods {
// Fixed storage is reserved before publishing damage; commit cannot allocate.
bool CanQueueEntityReactions(CMonstorUnit *unit);
void QueueEntityReaction(CMonstorUnit *unit, unsigned slot, int result) noexcept;
int  TakeEntityReaction(CMonstorUnit *unit, unsigned slot) noexcept;
int  ConsumeEntityDamage(CMonstorUnit *unit, int slot) noexcept;
void ClearEntityReactions(CMonstorUnit *unit, int slot = -1) noexcept;
} // namespace mods
