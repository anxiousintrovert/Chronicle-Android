# Package-owned quest signal example

Install both `signal-provider` and `signal-quest` as separate directories in your
save's `mods` folder. Both are disabled by default. Enable both in the shared mod
manager and restart. The quest declares a direct dependency on the provider;
selecting only the quest must report the missing/disabled provider.

Enter gameplay, open the shared mod actions page, and select **Send bridge signal**.
Return to gameplay so the queued action and subsequent event can run. The shared
quest page then shows **Bridge Signal** completed. Repeating the action does not
increase completed progress. Save and load to retain completion; keep both exact
package versions available for the save. No currency or items are awarded.

This is a manual author demonstration, not an actual challenge or cutscene system.
The provider emits `challenge.complete` with `{arena="bridge", success=true}`;
the consumer matches the provider ID, event name and payload fields. Publication
is transactional and delivery occurs on a later gameplay tick in the same world
context. A context change or load can discard pending signals; these are not a
durable message queue. Emit again after returning to gameplay if needed.

Future content extensions can emit their own signals after successful operations.
The loader and quest binding do not prescribe their content registries or formats.
See [host API](../../../docs/MOD_HOST_API.md) for event limits and ownership rules.
