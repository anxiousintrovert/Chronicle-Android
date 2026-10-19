# Norune Welcome Quest

Install this directory at `<save>/mods/norune-welcome/`. Open the manager and
restart if it was newly installed or enabled. Visit Norune to complete the quest
and receive 50 Gilda once for this save. Lua checks a persisted `reward_paid` key.

Requires API 1 and the capabilities in `mod.json`. Requests `economy.write`.
At the currency cap the balance is clamped and the reward is still marked paid.
Use an independent test save. Save/load must retain both the quest and guard.

To stop the mod, disable it and restart; saves made with its gameplay state need
this mod at version `1.0.0`. Removal does not reverse previously saved currency.
There are no declared dependencies or conflicts.
