# Combat Training example

Install this directory in your test save's mods folder, enable it explicitly in
the shared manager, and restart. New packages are disabled by default. Enter
gameplay and open the shared skill page (L2). No ranks or effects are granted by
registration. Request training for Focus, return to gameplay to process it, then
train Guard. Guard requires Focus rank 1. Each node has at most two ranks.

Training is deliberately free in this example. Focus grants 10% outgoing damage
per rank; Guard reduces dungeon life deductions through the native AddNowLife hook
by 5% per rank, including non-combat sources using that hook. It is not universal
armor. Other owners' effects multiply independently. The shared skill details
show current bonuses and prerequisites. No saved native stats are rewritten.

Use Reset example training on the shared actions page to reset both nodes. Requests
are deferred; return to gameplay to apply them. Save/load retains ranks. Keep this
exact package version for its saves. Other packages and trees are not reset.

The script demonstrates immutable registration, prerequisite checks, explicit
training, and resetting dependent ranks. It does not implement experience,
currency costs or automatic progression; authors must define those policies.

Validate from the repository root:

```sh
python tools/chronicle_mod.py check examples/mods/combat-training
```
