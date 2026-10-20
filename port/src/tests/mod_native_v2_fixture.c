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
    if (mode(host, "\"emit_custom\"") || mode(host, "\"fail_custom\"") || mode(host, "\"fail_emit_custom\"")) {
        if (*(int *)state != 1)
            return 1;
        if (!host->state_set(host->context, "emitted", "true", &result) ||
            !host->call(host->context, "events.emit", "{\"name\":\"pulse\",\"payload\":{\"answer\":42}}", &result))
            return 0;
        return !mode(host, "\"fail_emit_custom\"");
    }
    if (mode(host, "\"command\"") || mode(host, "\"fail_command\"")) {
        if (*(int *)state != 1)
            return 1;
        if (!increment(host, "command_calls") ||
            !host->call(host->context, "services.command", "{\"provider\":\"test.provider\",\"name\":\"reward\",\"arguments\":{\"value\":5}}", &result) ||
            !strstr(result, "\"value\":105"))
            return 0;
        return !mode(host, "\"fail_command\"");
    }
    if (mode(host, "\"quests\"")) {
        return host->call(host->context, "quests.list", "{}", &result) && strstr(result, "\"visit\"") &&
               host->call(host->context, "quests.get", "{\"id\":\"visit\"}", &result) && strstr(result, "\"goal\":2") &&
               !host->call(host->context, "quests.get", "{\"id\":\"foreign\"}", &result) &&
               host->call(host->context, "quests.register", "{\"id\":\"dynamic\",\"title\":\"Native objective\",\"description\":\"Collect treasures\",\"goal\":3}", &result) &&
               host->call(host->context, "quests.advance", "{\"id\":\"dynamic\",\"amount\":1}", &result) && strstr(result,"\"progress\":1") &&
               host->state_set(host->context, "quest_read", "true", &result);
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
    case CHRONICLE_EVENT_ENEMY_KILLED:
        if (!strstr(world, "\"event\":") || !strstr(world, "\"entity\":\"16\"") ||
            !strstr(world, "\"source\":\"scripted\""))
            return 0;
        key = "enemy_killed";
        break;
    case CHRONICLE_EVENT_CUSTOM:
        if (!strstr(world, "\"owner\":\"test.native.v2\"") || !strstr(world, "\"name\":\"pulse\"") ||
            !strstr(world, "\"payload\":{\"answer\":42}"))
            return 0;
        key = "custom";
        break;
    default:
        return 0;
    }
    if (!increment(host, key))
        return 0;
    host->log(host->context, key);
    if (which == CHRONICLE_EVENT_CUSTOM && mode(host, "\"fail_custom\"")) {
        host->call(host->context, "events.emit", "{\"name\":\"leaked\",\"payload\":{}}", &result);
        host->call(host->context, "economy.add", "{\"amount\":3}", &result);
        return 0;
    }
    if (which == CHRONICLE_EVENT_ENEMY_KILLED && mode(host, "\"fail_kill\"")) {
        host->call(host->context, "economy.add", "{\"amount\":3}", &result);
        return 0;
    }
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

#ifdef FIXTURE_MENU
static int run_action(const ChronicleModHostV2 *host, void *state, const char *world) {
    const char *result = 0;
    if (!state || !strstr(world, "\"playing\":true") || !strstr(world, "\"map\":"))
        return 0;
    if (mode(host, "\"audio\"") || mode(host, "\"audio_fail\"")) {
        char arguments[128];
        const char *handle, *end;
        if (!host->call(host->context, "audio.state", "{}", &result) || !strstr(result, "\"id\":\"tone\""))
            return 0;
        handle = strstr(result, "\"handle\":\"");
        if (handle) {
            handle += strlen("\"handle\":\"");
            end = strchr(handle, '"');
            if (!end || end - handle > 64) return 0;
            snprintf(arguments, sizeof(arguments), "{\"handle\":\"%.*s\"}", (int)(end - handle), handle);
            if (!host->call(host->context, "audio.stop", arguments, &result)) return 0;
        } else if (!host->call(host->context, "audio.play", "{\"id\":\"tone\",\"loop\":true,\"volume\":0.5}", &result)) {
            return 0;
        }
        if (!increment(host, "audio_actions")) return 0;
        return !mode(host, "\"audio_fail\"");
    }
    if (!increment(host, "actions"))
        return 0;
    if (!host->call(host->context, "economy.add", "{\"amount\":5}", &result))
        return 0;
    host->log(host->context, "native menu action called");
    return !mode(host, "\"fail_action\"");
}
#ifdef FIXTURE_MENU_BAD
#define MENU_TITLE "invalid\xff"
#else
#define MENU_TITLE "Native {cross} action"
#endif
static const ChronicleModMenuActionV1 menu_actions[] = {
    {sizeof(ChronicleModMenuActionV1), "run", MENU_TITLE, "Add five coins in a managed transaction.", run_action}};
static const ChronicleModMenuV1 menu = {
    sizeof(ChronicleModMenuV1), 1, "test.native.v2", 1, menu_actions};

CHRONICLE_PLUGIN_EXPORT const ChronicleModMenuV1 *ChronicleMod_GetMenuV1(void) { return &menu; }
#endif
