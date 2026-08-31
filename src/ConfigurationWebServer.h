#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    Preferences prefs;

    // Set from the web server task, read from the main loop.
    volatile bool mapReloadRequested = false;

public:
    ConfigurationWebServer() : server(80), prefs() {}
    ConfigurationWebServer(int port) : server(port), prefs() {}

    void Initialise();
    [[nodiscard]] const String GetStoredString(const char* key);

    // True once per request from the "Reload map now" button, so the caller can
    // do the actual downloading where blocking is allowed.
    [[nodiscard]] bool ConsumeMapReloadRequest();
};