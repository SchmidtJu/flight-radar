#pragma once

#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "LGFX.h"
#include "Viewport.h"

// Fetches the OpenStreetMap tiles covering the configured position once and
// keeps them as a ready-made sprite, so the render loop only has to blit.
class MapTileProvider
{
private:
    ConfigurationWebServer &configServer;
    HttpRequestManager &http;

    LGFX_Sprite mapSprite;

    Viewport viewport;

    bool ready = false;

    // The clock is usually not set when the first cache is written, and a file
    // without a timestamp could never expire.
    bool cacheTimestampPending = false;

    bool FetchTiles();

    void PostProcess();

    // Read once and shared by the filter and the cache header, so the two can
    // never disagree about what the stored picture contains.
    int BrightnessFromConfig();
    bool DarkFilterFromConfig();

    bool LoadFromCache();
    void SaveToCache();

public:
    MapTileProvider(ConfigurationWebServer &config, HttpRequestManager &httpManager, LGFX &tftGfx)
        : configServer(config), http(httpManager), mapSprite(&tftGfx)
    {
    }

    ~MapTileProvider() = default;

    // ignoreCache is what the "Reload map now" button uses: it refetches even
    // when a valid cache exists, and overwrites it afterwards.
    void Initialise(bool ignoreCache = false);

    // False means there is no usable map and callers fall back to a plain radar
    // picture. Never a reason to stop working.
    [[nodiscard]] bool IsReady() const { return ready; }

    [[nodiscard]] const Viewport &GetViewport() const { return viewport; }

    // Call from the main loop: fills in a missing cache timestamp once SNTP has
    // answered, and does nothing at all afterwards.
    void UpdateCacheTimestampWhenClockArrives();

    void DrawTo(LGFX_Sprite &dst);
};
