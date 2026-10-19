#include "mods/native_plugin.h"

static int start(const ChronicleModHostV1 *host, void **state) {
    const char *result = 0;
    *state = 0;
    host->log(host->context, "fixture started");
    /* Native game calls at package load must be rejected. */
    return host->abi == 1 && !host->call(host->context, "economy.get", "{}", &result);
}

static int tick(const ChronicleModHostV1 *host, void *state) {
    const char *result = 0;
    (void)state;
    host->log(host->context, "fixture tick");
    return host->available(host->context, "economy.add") &&
           host->call(host->context, "economy.get", "{}", &result) &&
           host->call(host->context, "economy.add", "{\"amount\":7}", &result);
}

static void stop(const ChronicleModHostV1 *host, void *state) {
    (void)state;
    host->log(host->context, "fixture stopped");
}

static const ChronicleModPluginV1 plugin = {sizeof(ChronicleModPluginV1), 1, "test.native", start, tick, stop};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV1 *ChronicleMod_GetPluginV1(void) { return &plugin; }
