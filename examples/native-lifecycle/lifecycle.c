#include "mods/native_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int start(const ChronicleModHostV2 *host, void **state) {
    *state = 0;
    host->log(host->context, "Native lifecycle example started (ABI 2)");
    return host->abi == 2 && host->size >= sizeof(*host);
}

static int tick(const ChronicleModHostV2 *host, void *state) {
    (void)host;
    (void)state;
    return 1;
}

static int event(const ChronicleModHostV2 *host, void *state, uint32_t which, const char *world_json) {
    const char *value = 0;
    char *end = 0;
    char number[32], message[128];
    long count = 0;
    (void)state;
    (void)world_json;
    if (!host->state_get(host->context, "visits", &value))
        return 0;
    if (strcmp(value, "null") != 0) {
        count = strtol(value, &end, 10);
        if (end == value || *end || count < 0 || count > 1000000)
            return 0;
    }
    if (which == CHRONICLE_EVENT_GAME_ENTER) {
        if (count < 1000000)
            ++count;
        snprintf(number, sizeof(number), "%ld", count);
        if (!host->state_set(host->context, "visits", number, &value))
            return 0;
    }
    snprintf(message, sizeof(message), "%s: visits=%ld", which == CHRONICLE_EVENT_SAVE_LOADED ? "Save restored" : "Gameplay entered", count);
    host->log(host->context, message);
    return 1;
}

static void stop(const ChronicleModHostV2 *host, void *state) {
    (void)state;
    host->log(host->context, "Native lifecycle example stopped");
}

static const ChronicleModPluginV2 plugin = {
    sizeof(ChronicleModPluginV2), 2, "example.native-lifecycle", start, tick, stop,
    CHRONICLE_EVENT_GAME_ENTER | CHRONICLE_EVENT_SAVE_LOADED, event};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV2 *ChronicleMod_GetPluginV2(void) { return &plugin; }
