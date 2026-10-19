#ifndef CHRONICLE_NATIVE_PLUGIN_H
#define CHRONICLE_NATIVE_PLUGIN_H
#include <stdint.h>
#ifdef _WIN32
#define CHRONICLE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CHRONICLE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Chronicle ABI 1. Same-thread calls only. Strings are UTF-8. Host result pointers
 * remain valid until the next host call. Never free them. Native code is trusted
 * in-process code, not sandboxed. No engine pointers or C++ ABI cross this boundary. */
typedef struct ChronicleModHostV1 {
    uint32_t size, abi;
    void    *context;
    int (*available)(void *, const char *operation);
    /* Returns 1 for success, 0 for a JSON error string. Writes are queued until
     * the callback succeeds; ordinary writes return {"queued":true}.
     * services.query/command return the exported service's JSON object. */
    int (*call)(void *, const char *operation, const char *arguments_json, const char **result_json);
    void (*log)(void *, const char *message);
} ChronicleModHostV1;

typedef struct ChronicleModPluginV1 {
    uint32_t    size, abi;
    const char *id; /* Must exactly match the package id. */
    int (*start)(const ChronicleModHostV1 *, void **state);
    int (*tick)(const ChronicleModHostV1 *, void *state); /* 0 stops this plugin. */
    void (*stop)(const ChronicleModHostV1 *, void *state);
} ChronicleModPluginV1;

typedef const ChronicleModPluginV1 *(*ChronicleModEntryV1)(void);
/* Export ChronicleMod_GetPluginV1 returning a descriptor with static lifetime. */

/* ABI 2 is a separate entry point and table; ABI 1 layouts never change.
 * State access is allowed inside tick/event/menu/service callbacks. It addresses this
 * package's scalar save values (shared with Lua get/set), not plugin heap memory. */
typedef struct ChronicleModHostV2 {
    uint32_t size, abi;
    void    *context;
    int (*available)(void *, const char *operation);
    int (*call)(void *, const char *operation, const char *arguments_json, const char **result_json);
    void (*log)(void *, const char *message);
    /* Missing keys return JSON null. Set accepts a scalar; null deletes a key.
     * Both return 1 on success, 0 with a JSON error. Results are host-owned and
     * valid until the next result-producing host call on this package. */
    int (*state_get)(void *, const char *key, const char **result_json);
    int (*state_set)(void *, const char *key, const char *value_json, const char **result_json);
} ChronicleModHostV2;

enum ChronicleModEventV2 {
    CHRONICLE_EVENT_GAME_ENTER = 1u << 0,
    CHRONICLE_EVENT_GAME_LEAVE = 1u << 1,
    CHRONICLE_EVENT_MAP_ENTER = 1u << 2,
    CHRONICLE_EVENT_FLOOR_ENTER = 1u << 3,
    CHRONICLE_EVENT_SAVE_LOADED = 1u << 4,
    CHRONICLE_EVENT_ENEMY_KILLED = 1u << 5,
    CHRONICLE_EVENT_CUSTOM = 1u << 6, /* event: {owner,name,payload}; own package or declared dependencies only. */
    CHRONICLE_EVENT_ALL = (1u << 7) - 1
};

typedef struct ChronicleModPluginV2 {
    uint32_t    size, abi;
    const char *id;
    int (*start)(const ChronicleModHostV2 *, void **state);
    int (*tick)(const ChronicleModHostV2 *, void *state);
    void (*stop)(const ChronicleModHostV2 *, void *state);
    uint32_t event_mask; /* OR of requested ChronicleModEventV2 bits, or zero. */
    /* event is one bit, world_json is a callback-lifetime world snapshot.
     * Return 0 to roll back this callback's managed writes and stop the package.
     * GAME_LEAVE permits owned state access but denies game operations. */
    int (*event)(const ChronicleModHostV2 *, void *state, uint32_t event, const char *world_json);
} ChronicleModPluginV2;

typedef const ChronicleModPluginV2 *(*ChronicleModEntryV2)(void);
/* Export ChronicleMod_GetPluginV2. Hosts prefer V2 when both entries are present;
 * an invalid V2 descriptor fails explicitly instead of falling back to V1. */

/* Optional menu extension for ABI 2 plugins. All descriptors and strings have
 * static lifetime. Export ChronicleMod_GetMenuV1; no existing ABI layout changes.
 * Actions execute later on the game thread, never in the menu input handler. */
typedef struct ChronicleModMenuActionV1 {
    uint32_t    size;
    const char *id, *title, *description;
    /* Same plugin state as start/tick. world_json has callback lifetime.
     * Return 0 to roll back managed changes and stop the package. */
    int (*run)(const ChronicleModHostV2 *, void *state, const char *world_json);
} ChronicleModMenuActionV1;

typedef struct ChronicleModMenuV1 {
    uint32_t                        size, abi;
    const char                     *id;    /* Exact package ID. */
    uint32_t                        count; /* At most 32, shared with this package's Lua actions. */
    const ChronicleModMenuActionV1 *actions;
} ChronicleModMenuV1;

typedef const ChronicleModMenuV1 *(*ChronicleModMenuEntryV1)(void);

/* Optional service extension for ABI 2. Export ChronicleMod_GetServicesV1.
 * Descriptor arrays and names have static lifetime. No existing layout changes. */
enum ChronicleModServiceKindV1 {
    CHRONICLE_SERVICE_QUERY = 0,
    CHRONICLE_SERVICE_COMMAND = 1
};

typedef struct ChronicleModServiceV1 {
    uint32_t    size, kind;
    const char *name;
    /* args_json is a bounded object; caller_id is the trusted immediate caller.
     * QUERY denies host managed/game writes. COMMAND shares the outer transaction.
     * Return 1 and a UTF-8 JSON object (<=16 KiB), or 0 for failure.
     * Result storage belongs to the plugin and must remain valid until its next
     * service invocation or stop; never return stack-local storage. */
    int (*invoke)(const ChronicleModHostV2 *, void *state, const char *args_json,
                  const char *caller_id, const char **result_json);
} ChronicleModServiceV1;

typedef struct ChronicleModServicesV1 {
    uint32_t                     size, abi;
    const char                  *id;    /* Exact package ID. */
    uint32_t                     count; /* At most 32, shared with Lua query/command exports. */
    const ChronicleModServiceV1 *services;
} ChronicleModServicesV1;

typedef const ChronicleModServicesV1 *(*ChronicleModServicesEntryV1)(void);
#ifdef __cplusplus
}
#endif
#endif
