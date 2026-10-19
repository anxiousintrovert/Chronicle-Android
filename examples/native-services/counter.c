#include "mods/native_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Counter {
    char result[80];
};

static int start(const ChronicleModHostV2 *host, void **state) {
    if (host->abi != 2 || host->size < sizeof(*host))
        return 0;
    *state = calloc(1, sizeof(struct Counter));
    return *state != NULL;
}

static int tick(const ChronicleModHostV2 *host, void *state) {
    (void)host;
    (void)state;
    return 1;
}

static void stop(const ChronicleModHostV2 *host, void *state) {
    (void)host;
    free(state);
}

static int read_count(const ChronicleModHostV2 *host, unsigned long *count) {
    const char *value = NULL;
    char *end = NULL;
    if (!host->state_get(host->context, "count", &value))
        return 0;
    if (strcmp(value, "null") == 0) {
        *count = 0;
        return 1;
    }
    if (*value < '0' || *value > '9')
        return 0;
    *count = strtoul(value, &end, 10);
    return end != value && *end == '\0' && *count <= 1000000;
}

static int get(const ChronicleModHostV2 *host, void *state, const char *args,
               const char *caller, const char **result) {
    struct Counter *counter = state;
    unsigned long count;
    (void)caller;
    if (strcmp(args, "{}") != 0 || !read_count(host, &count))
        return 0;
    snprintf(counter->result, sizeof(counter->result), "{\"count\":%lu}", count);
    *result = counter->result;
    return 1;
}

static int increment(const ChronicleModHostV2 *host, void *state, const char *args,
                     const char *caller, const char **result) {
    struct Counter *counter = state;
    unsigned long count;
    char value[32];
    const char *response = NULL;
    (void)caller;
    if (strcmp(args, "{}") != 0 || !read_count(host, &count))
        return 0;
    if (count == 1000000) {
        *result = "{\"accepted\":false}";
        return 1;
    }
    snprintf(value, sizeof(value), "%lu", ++count);
    if (!host->state_set(host->context, "count", value, &response))
        return 0;
    snprintf(counter->result, sizeof(counter->result), "{\"accepted\":true,\"count\":%lu}", count);
    *result = counter->result;
    return 1;
}

static int reset(const ChronicleModHostV2 *host, void *state, const char *world) {
    const char *result = NULL;
    (void)state;
    (void)world;
    if (!host->state_set(host->context, "count", "0", &result))
        return 0;
    host->log(host->context, "Service counter reset");
    return 1;
}

static const ChronicleModPluginV2 plugin = {
    sizeof(ChronicleModPluginV2), 2, "example.native-services", start, tick, stop, 0, NULL};
static const ChronicleModServiceV1 service_entries[] = {
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_QUERY, "counter", get},
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_COMMAND, "increment", increment}};
static const ChronicleModServicesV1 services = {
    sizeof(ChronicleModServicesV1), 1, "example.native-services", 2, service_entries};
static const ChronicleModMenuActionV1 actions[] = {
    {sizeof(ChronicleModMenuActionV1), "reset", "Reset service counter", "Reset only this plugin's owned counter.", reset}};
static const ChronicleModMenuV1 menu = {
    sizeof(ChronicleModMenuV1), 1, "example.native-services", 1, actions};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV2 *ChronicleMod_GetPluginV2(void) { return &plugin; }

CHRONICLE_PLUGIN_EXPORT const ChronicleModServicesV1 *ChronicleMod_GetServicesV1(void) { return &services; }

CHRONICLE_PLUGIN_EXPORT const ChronicleModMenuV1 *ChronicleMod_GetMenuV1(void) { return &menu; }
