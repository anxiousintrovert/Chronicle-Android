#include "mods/native_plugin.h"

static int start(const ChronicleModHostV1 *host, void **state) {
    *state = 0;
    host->log(host->context, "Native probe loaded; no gameplay changes requested.");
    return host->abi == 1;
}

static int tick(const ChronicleModHostV1 *host, void *state) {
    (void)host;
    (void)state;
    return 1;
}

static void stop(const ChronicleModHostV1 *host, void *state) {
    (void)state;
    host->log(host->context, "Native probe stopped.");
}

static const ChronicleModPluginV1 plugin = {sizeof(ChronicleModPluginV1), 1, "example.native-probe", start, tick, stop};

CHRONICLE_PLUGIN_EXPORT const ChronicleModPluginV1 *ChronicleMod_GetPluginV1(void) { return &plugin; }
