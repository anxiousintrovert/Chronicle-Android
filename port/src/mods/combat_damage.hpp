#pragma once
#include "damage_modifiers.hpp"
class CMonstorUnit;
class CUserStatus;
namespace mods {
struct CombatDamage {
    DamageModifiers outgoing, incoming;
    std::uint64_t epoch = 0, frame = 0;
    bool Empty() const { return outgoing.Empty() && incoming.Empty(); }
};
CombatDamage &OwnedCombatDamage();
void RetainCombatDamageOwners(bool advance_frame = false);
float ModifyOutgoingDamage(const CMonstorUnit *, int actor, int character, float damage) noexcept;
// True suppresses an explicitly blocked HP deduction, including interpolation.
bool ModifyIncomingDamage(const CUserStatus *, int character, short &amount) noexcept;
}
