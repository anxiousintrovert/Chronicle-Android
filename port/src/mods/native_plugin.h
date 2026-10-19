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
#ifdef __cplusplus
}
#endif
#endif
