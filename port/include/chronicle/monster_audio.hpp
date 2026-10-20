#pragma once
namespace chronicle::monsters {
// Floor-load boundary only. Retail-to-retail is a no-op; leaving expanded
// routing queues stops for its outstanding leases before restoring byte IDs.
void PrepareEffectVoicesForMonsterSlots(unsigned slots);
bool ExpandedEffectVoicesActive();
}
