# Planned actor lifecycle events

This is an implementation plan, not an available capability. Existing enemy-death
subscriptions remain the only public actor event contract. Do not advertise spawn
or removal subscriptions until their capture, cancellation and runtime tests land.

## Source audit

- `port/src/mods/entity_lifecycle.cpp`: `SetupViewMonstor` calls
  `EntitySlotConstructed` before script reset, character assignment, actor fields,
  event-script execution and alive-count update. The identity notification is not
  a completed-spawn event. The successful return is a candidate native capture
  point, subject to validation of final actor state after event-script execution.
- `port/src/mods/entity_tracking.cpp`: `TrackEntities` can discover actors already
  present when tracking starts. This must not synthesize historical spawn events.
  Slot construction replaces generations, and unit clear invalidates identities
  and cancels queued deaths. A subscriber must not mistake slot reuse for the
  continued lifetime of the previous actor.
- `port/src/mods/actor_spawn.cpp`: owned spawning publishes its ownership reservation
  after native creation. Capturing at native setup alone would precede owned
  metadata and optional spawn tuning. Owned spawn notification must represent the
  completed owned operation without emitting a second native notification.
- `port/src/mods/actor_cleanup.cpp`: owned retirement rejects unsafe cleanup before
  clearing references, resetting the native slot, decrementing alive count and
  retiring the identity. A successful explicit/owner cleanup is distinct from an
  enemy death; neither should be inferred solely from an HP change.
- `port/src/mods/entity_events.hpp` and `game.cpp`: enemy deaths already demonstrate
  bounded value capture, deferred dispatch, overflow reporting and context filtering.
  Public lifecycle events need their own lifecycle rules rather than silently
  changing that existing death contract.

## Implementation sequence

1. Introduce bounded capture records with event kind, generation handle, native
   unit identity and dungeon/floor context. Capture final values, not mutable
   actor pointers for subscribers. Keep capture disabled without live subscribers.
2. Bind successful native and owned spawn paths without double publication. Do not
   emit for initial tracking discovery, rejected reservations or failed commands.
   Review native script-driven removal paths before claiming all native removals.
3. Capture successful explicit and owner-cleanup retirement with a documented
   reason. Do not promise that the retired handle remains queryable. Floor/session
   teardown cancels stale records; it is not an enemy-killed event.
4. Drain before callbacks, preserving a later-tick boundary for events produced by
   callback commits. Filter session/unit/context changes before dispatch. Bound
   per-tick delivery and report overflow. Callback failures must not resurrect or
   replay a completed native lifecycle operation.
5. Add Lua and ABI 2 opt-in subscriptions and capability discovery only after the
   native producers and cancellation rules are tested. Keep permissions, payloads
   and observation scope explicit. Use existing shared-menu author actions for
   scenario controls; do not create another menu implementation.

## Acceptance evidence

- No subscribers: no queue activation, actor enumeration, allocations or RNG use
  introduced by lifecycle observation; native spawn/cleanup outcomes unchanged.
- Existing actors do not generate synthetic spawn notifications on subscription.
- Successful native/owned spawn emits once with final identity and tuning; failed
  spawn emits none. Slot reuse produces a different generation.
- Explicit retirement and owner cleanup emit only after successful cleanup.
  Rejected retirement and enemy death are distinguishable.
- Same-callback production is deferred; floor/back-floor/load/restart changes
  cancel stale records. Overflow is bounded and visible in diagnostics.
- Lua and native listeners receive detached values, with normal ownership and
  rollback behavior. Native plugin trust remains separate from enablement.
- Clang unit/runtime tests plus an isolated PAL spawn/despawn/back-floor scenario
  pass. Wider platform and native-removal coverage remain explicit if unverified.

All implementation belongs outside PS2 directories. This work must not define
custom item, weapon or cutscene registries reserved for contributor extensions.
