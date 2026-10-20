#include "combat_damage.hpp"
#include "model_identity.hpp"
#include "runtime.hpp"
#include "monstorunit.hpp"
#include "userstatus.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
extern s32 mode;
namespace mods {
namespace {
CombatDamage combat;
bool Active(const DamageModifiers &rules) {
    return !rules.Empty() && mode == GAME_MODE_DUNGEON && combat.epoch == Scripts().Epoch();
}
int Weapon(const CUserStatus *status, int character) {
    if (!status || character < 0 || character >= 6) return -1;
    const int slot = status->equipped_weapon_slot[character];
    return slot >= 0 && slot < 11 ? status->chara_weapons[character][slot].item_no : -1;
}
}
CombatDamage &OwnedCombatDamage() { return combat; }
void RetainCombatDamageOwners(bool advance_frame) {
    if (combat.Empty()) return;
    if (combat.epoch != Scripts().Epoch()) { combat = {}; return; }
    const auto owners = Scripts().ActiveOwners();
    combat.outgoing.Retain(owners); combat.incoming.Retain(owners);
    if (advance_frame && mode == GAME_MODE_DUNGEON && combat.frame != std::numeric_limits<std::uint64_t>::max()) ++combat.frame;
}
float ModifyOutgoingDamage(const CMonstorUnit *unit, int actor, int character, float damage) noexcept {
    if (!Active(combat.outgoing) || !unit || actor < 0 || actor >= unit->SlotLimit()) return damage;
    const auto &target = unit->monster[actor];
    if (target.state == -1) return damage;
    DamageContext context;
    context.weapon = Weapon(UserStatus, character);
    context.character = character >= 0 && character < 6 ? character : -1;
    context.model = NativeActorModelId(unit, actor);
    context.hp = target.hp; context.max_hp = target.max_hp; context.frame = combat.frame;
    return combat.outgoing.Apply(damage, context);
}
bool ModifyIncomingDamage(const CUserStatus *status, int character, short &amount) noexcept {
    if (!Active(combat.incoming) || !status || status != UserStatus || character < 0 || character >= 6 || amount >= 0) return false;
    DamageContext context;
    context.weapon = Weapon(status, character); context.character = character;
    context.hp = status->life_step[character] ? status->next_hp[character] : status->hp[character];
    context.max_hp = status->max_hp[character]; context.frame = combat.frame;
    const float original = -static_cast<float>(amount);
    const float result = combat.incoming.Apply(original, context);
    if (result == original) return false;
    if (result <= 0) { amount = 0; return true; }
    amount = static_cast<short>(-std::clamp(std::lround(result), 1L, 32767L));
    return false;
}
}
