#pragma once

#include <map>

#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "LGFX.h"
#include "Viewport.h"

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.2;

    // Derived from the configuration above and shared in spirit with the map:
    // both sides project against this, so they cannot drift apart.
    Viewport viewport;

    std::map<String, TrackedAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;
    bool displayRadarCircles = true;

    // Which fields the details screen shows
    bool detailIcon = true;
    bool detailCallsign = true;
    bool detailAltitude = true;
    bool detailSpeed = true;
    bool detailHeading = true;
    bool detailIcao = true;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer &configServer;
    OpenSkyAuthTokenHandler &authHandler;
    HttpRequestManager &http;
    LGFX &tft;

    void DrawRadarCircles(LGFX_Sprite &backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked, bool selected) const;
    void DrawAircraftTriangle(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const;

public:
    AircraftManager(ConfigurationWebServer &config, OpenSkyAuthTokenHandler &auth, HttpRequestManager &httpManager, LGFX &tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite &backbuffer);
    uint32_t GetAircraftColour(const TrackedAircraft &tracked) const;
    void SelectNextAircraft();
    void SelectPreviousAircraft();
    void DrawDetails(LGFX_Sprite &backbuffer);
    void EncoderClick();
};