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
     * the callback succeeds; the response for a write is {"queued":true}. */
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
 * State access is allowed only inside tick/event callbacks. It addresses this
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
    CHRONICLE_EVENT_ALL = (1u << 5) - 1
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
#ifdef __cplusplus
}
#endif
#endif
