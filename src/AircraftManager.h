#pragma once

#include <map>

#include "models/TrackedAircraft.h"
#include "AircraftInfoProvider.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "OpenSkyBudget.h"
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
    bool detailType = true;
    bool detailRoute = true;
    bool detailAirportNames = true;
    bool detailAltitude = true;
    bool detailVerticalSpeed = true;
    bool detailSpeed = true;
    bool detailSpeedKmh = true;
    bool detailHeading = true;
    bool detailRegistration = true;
    bool detailCallsign = true;
    bool detailIcao = true;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    // Type and route come from a second API, requested only for the aircraft
    // the details screen currently shows.
    AircraftInfoProvider infoProvider;
    bool infoLookupPending = false;
    unsigned long infoLookupRequestedAt = 0;

    ConfigurationWebServer &configServer;
    OpenSkyAuthTokenHandler &authHandler;
    HttpRequestManager &http;
    LGFX &tft;

    // Whether any row on the details screen needs the second API at all. Both
    // the request and the drawing ask, so they cannot disagree about it.
    bool NeedsAircraftInfo() const;

    void RequestInfoLookup();
    void ResolveSelectedAircraftInfo(unsigned long now);
    void DrawRadarCircles(LGFX_Sprite &backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked, bool selected) const;
    void DrawAircraftTriangle(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const;
    void DrawHelicopterIcon(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked, uint32_t colour) const;

public:
    AircraftManager(ConfigurationWebServer &config, OpenSkyAuthTokenHandler &auth, HttpRequestManager &httpManager, LGFX &tftGfx)
        : infoProvider(httpManager), configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
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