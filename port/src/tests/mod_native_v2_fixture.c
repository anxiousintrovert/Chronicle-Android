#include "mods/native_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int start(const ChronicleModHostV2 *host, void **state) {
    const char *result = 0;
    *state = calloc(1, sizeof(int));
    host->log(host->context, "v2 started");
    return *state && host->abi == 2 && host->size >= sizeof(*host) &&
           !host->state_get(host->context, "start", &result) &&
           !host->state_set(host->context, "start", "true", &result) &&
           !host->call(host->context, "economy.get", "{}", &result);
}

static int mode(const ChronicleModHostV2 *host, const char *expected) {
    const char *result = 0;
    return host->state_get(host->context, "mode", &result) && strcmp(result, expected) == 0;
}

static int increment(const ChronicleModHostV2 *host, const char *key) {
    const char *result = 0;
    char value[32];
    if (!host->state_get(host->context, key, &result))
        return 0;
    snprintf(value, sizeof(value), "%d", atoi(result) + 1);
    return host->state_set(host->context, key, value, &result);
}

static int tick(const ChronicleModHostV2 *host, void *state) {
    const char *result = 0;
    ++*(int *)state;
    if (mode(host, "\"key_limit\"")) {
        return !host->state_set(host->context, "new_key", "true", &result) &&
               host->state_set(host->context, "seed", "42", &result) &&
               host->state_set(host->context, "missing", "null", &result) &&
               host->state_set(host->context, "seed", "null", &result) &&
               host->state_set(host->context, "new_key", "true", &result);
    }
    if (mode(host, "\"host_limit\"")) {
        int successes = 0;
        while (host->state_get(host->context, "missing", &result))
            ++successes;
        /* Two mode queries above share the same 128-call budget. */
        if (successes != 126)
            return 0;
        host->log(host->context, "v2 shared host budget passed");
        return 1;
    }
    if (mode(host, "\"invalid_values\"")) {
        char too_long[260];
        memset(too_long, 'x', sizeof(too_long));
        too_long[0] = '"';
        too_long[258] = '"';
        too_long[259] = 0;
        return !host->state_set(host->context, "bad key", "1", &result) &&
               !host->state_set(host->context, "bad", "[]", &result) &&
               !host->state_set(host->context, "bad", "{}", &result) &&
               !host->state_set(host->context, "bad", "1e9999", &result) &&
               !host->state_set(host->context, "bad", too_long, &result) &&
               !host->state_set(host->context, "bad", "\"\\u0000\"", &result) &&
               !host->state_set(host->context, "bad", 0, &result) &&
               host->state_set(host->context, "valid", "\"snowman \\u2603\"", &result) &&
               host->state_get(host->context, "absent", &result) && strcmp(result, "null") == 0;
    }
    if (!increment(host, "ticks"))
        return 0;
    host->log(host->context, "v2 tick");
    if (!host->call(host->context, "economy.add", "{\"amount\":7}", &result))
        return 0;
    return !mode(host, "\"fail_tick\"");
}

static int event(const ChronicleModHostV2 *host, void *state, uint32_t which, const char *world) {
    const char *result = 0;
    const char *key = 0;
    (void)state;
    if (mode(host, "\"key_limit\""))
        return 1;
    if (!strstr(world, "\"dungeon\":") || !strstr(world, "\"floor\":") || !strstr(world, "\"mode\":"))
        return 0;
    switch (which) {
    case CHRONICLE_EVENT_GAME_ENTER:
        key = "game_enter";
        break;
    case CHRONICLE_EVENT_GAME_LEAVE:
        key = "game_leave";
        break;
    case CHRONICLE_EVENT_MAP_ENTER:
        key = "map_enter";
        break;
    case CHRONICLE_EVENT_FLOOR_ENTER:
        key = "floor_enter";
        break;
    case CHRONICLE_EVENT_SAVE_LOADED:
        key = "save_loaded";
        break;
    default:
        return 0;
    }
    if (!increment(host, key))
        return 0;
    host->log(host->context, key);
    if (which == CHRONICLE_EVENT_GAME_LEAVE) {
        return strstr(world, "\"playing\":false") &&
               !host->call(host->context, "economy.get", "{}", &result);
    }
    if (!strstr(world, "\"playing\":true"))
        return 0;
    if (which == CHRONICLE_EVENT_GAME_ENTER && mode(host, "\"fail_event\"")) {
        host->call(host->context, "economy.add", "{\"amount\":3}", &result);
        return 0;
    }
    return 1;
}

static void stop(const ChronicleModHostV2 *host, void *state) {
    const char *result = 0;
    if (!host->state_get(host->context, "ticks", &result) &&
        !host->state_set(host->context, "stopped", "true", &result) &&
        !host->call(host->context, "economy.add", "{\"amount\":1}", &result)) {
        host->log(host->context, "v2 stopped outside transaction");
    }
    free(state);
}

#ifndef FIXTURE_ABI
#define FIXTURE_ABI 2
#endif
#ifndef FIXTURE_EVENTS
#define FIXTURE_EVENTS CHRONICLE_EVENT_ALL
#endif
static const ChronicleModPluginV2 plugin = {
    sizeof(ChronicleModPluginV2), FIXTURE_ABI, "test.native.v2", start, tick, stop, FIXTURE_EVENTS, event};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV2 *ChronicleMod_GetPluginV2(void) { return &plugin; }

/* A V2 host must prefer the valid V2 entry, and never fall back after rejecting it. */
CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV1 *ChronicleMod_GetPluginV1(void) { return 0; }
