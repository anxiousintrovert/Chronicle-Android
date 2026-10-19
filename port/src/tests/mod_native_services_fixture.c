#include "mods/native_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct State {
    char result[512];
    char large[17000];
};

static int start(const ChronicleModHostV2 *h, void **state) {
    const char *r = 0;
    *state = calloc(1, sizeof(struct State));
    return *state && !h->state_get(h->context, "value", &r);
}

static int tick(const ChronicleModHostV2 *h, void *s) {
    (void)h;
    (void)s;
    return 1;
}

static void stop(const ChronicleModHostV2 *h, void *s) {
    (void)h;
    free(s);
}

static int inspect(const ChronicleModHostV2 *h, void *opaque, const char *args, const char *caller, const char **out) {
    struct State *s = opaque;
    const char *value = 0;
    int n;
    if (strstr(args, "bad_array")) {
        *out = "[]";
        return 1;
    }
    if (strstr(args, "bad_json")) {
        *out = "{";
        return 1;
    }
    if (strstr(args, "bad_utf8")) {
        *out = "{\"v\":\"\xff\"}";
        return 1;
    }
    if (strstr(args, "null_result"))
        return 1;
    if (strstr(args, "oversize")) {
        memset(s->large, 'x', sizeof(s->large) - 1);
        *out = s->large;
        return 1;
    }
    if (strstr(args, "fail"))
        return 0;
    if (!h->state_get(h->context, "value", &value))
        return 0;
    n = atoi(value);
    if (h->state_set(h->context, "value", "99", &value) ||
        h->call(h->context, "economy.add", "{\"amount\":99}", &value) ||
        h->call(h->context, "events.emit", "{\"name\":\"leak\",\"payload\":{}}", &value) ||
        h->call(h->context, "services.command", "{\"provider\":\"test.base\",\"name\":\"change\",\"arguments\":{}}", &value))
        return 0;
    if (!h->call(h->context, "economy.get", "{}", &value) || !strstr(value, "money"))
        return 0;
    snprintf(s->result, sizeof(s->result), "{\"value\":%d,\"caller\":\"%s\"}", n, caller);
    *out = s->result;
    return 1;
}

static int change(const ChronicleModHostV2 *h, void *opaque, const char *args, const char *caller, const char **out) {
    struct State *s = opaque;
    const char *r = 0;
    if (!h->state_set(h->context, "value", "9", &r) ||
        !h->call(h->context, "economy.add", "{\"amount\":7}", &r) ||
        !h->call(h->context, "events.emit", "{\"name\":\"changed\",\"payload\":{}}", &r))
        return 0;
    if (strstr(args, "fail"))
        return 0;
    if (strstr(args, "bad_array")) {
        *out = "[]";
        return 1;
    }
    snprintf(s->result, sizeof(s->result), "{\"value\":9,\"caller\":\"%s\"}", caller);
    *out = s->result;
    return 1;
}

static int relay_query(const ChronicleModHostV2 *h, void *s, const char *args, const char *caller, const char **out) {
    (void)s;
    (void)args;
    (void)caller;
    return h->call(h->context, "services.query", "{\"provider\":\"test.base\",\"name\":\"read\",\"arguments\":{}}", out);
}

static int relay_command(const ChronicleModHostV2 *h, void *s, const char *args, const char *caller, const char **out) {
    (void)s;
    (void)args;
    (void)caller;
    return h->call(h->context, "services.command", "{\"provider\":\"test.base\",\"name\":\"change\",\"arguments\":{}}", out);
}

static int budget(const ChronicleModHostV2 *h, void *opaque, const char *args, const char *caller, const char **out) {
    struct State *s = opaque;
    const char *r = 0;
    int count = 0;
    (void)args;
    (void)caller;
    while (h->call(h->context, "services.query", "{\"provider\":\"test.base\",\"name\":\"read\",\"arguments\":{}}", &r))
        ++count;
    snprintf(s->result, sizeof(s->result), "{\"calls\":%d}", count);
    *out = s->result;
    return 1;
}

static const ChronicleModPluginV2 plugin = {sizeof(ChronicleModPluginV2), 2, "test.native.services", start, tick, stop, 0, 0};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV2 *ChronicleMod_GetPluginV2(void) { return &plugin; }
#ifndef FIXTURE_SERVICE_KIND
#define FIXTURE_SERVICE_KIND CHRONICLE_SERVICE_QUERY
#endif
#ifndef FIXTURE_SERVICE_ABI
#define FIXTURE_SERVICE_ABI 1
#endif
static const ChronicleModServiceV1 entries[] = {
    {sizeof(ChronicleModServiceV1), FIXTURE_SERVICE_KIND, "inspect", inspect},
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_COMMAND, "change", change},
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_QUERY, "relay", relay_query},
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_COMMAND, "relay", relay_command},
    {sizeof(ChronicleModServiceV1), CHRONICLE_SERVICE_QUERY, "budget", budget}};
static const ChronicleModServicesV1 services = {sizeof(ChronicleModServicesV1), FIXTURE_SERVICE_ABI, "test.native.services", sizeof(entries) / sizeof(entries[0]), entries};

CHRONICLE_PLUGIN_EXPORT const ChronicleModServicesV1 *ChronicleMod_GetServicesV1(void) { return &services; }
