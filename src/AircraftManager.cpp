#include "AircraftManager.h"
#include <vector>

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

#include <ArduinoJson.h>

enum ScreenMode
{
    SCREEN_RADAR,
    SCREEN_DETAILS
};

ScreenMode currentScreen = SCREEN_RADAR;

int selectedAircraftIndex = 0;
std::vector<TrackedAircraft *> visibleAircraft;

static const uint32_t AircraftColours[] =
    {
        lgfx::color888(0, 255, 128),   // Radar green
        lgfx::color888(0, 220, 255),   // Cyan
        lgfx::color888(0, 128, 255),   // Electric blue
        lgfx::color888(100, 200, 255), // Sky blue
        lgfx::color888(160, 130, 255), // Soft violet
        lgfx::color888(220, 80, 255),  // Magenta
        lgfx::color888(255, 50, 150),  // Hot pink
        lgfx::color888(255, 60, 60),   // Coral red
        lgfx::color888(255, 120, 0),   // Burnt orange
        lgfx::color888(255, 190, 0),   // Amber
        lgfx::color888(255, 235, 0),   // Neon yellow
        lgfx::color888(180, 255, 0),   // Acid lime
        lgfx::color888(80, 255, 180),  // Mint
        lgfx::color888(0, 200, 180),   // Teal
        lgfx::color888(50, 180, 255),  // Cornflower
        lgfx::color888(255, 160, 100), // Peach
        lgfx::color888(255, 210, 160), // Warm cream
        lgfx::color888(140, 255, 140), // Pale green
        lgfx::color888(200, 160, 255), // Lavender
        lgfx::color888(255, 120, 200)  // Pink
};

// Every entry in the palette above is a saturated hue, so a highlight cannot
// win on hue alone - green now belongs to several aircraft at once. White is the
// one colour the palette does not reach, and the ring settles the question even
// where aircraft overlap or the map runs pale underneath.
static const uint32_t SelectionColour = lgfx::color888(255, 255, 255);
constexpr int SelectionRingRadius = 12;

void AircraftManager::Initialise()
{
    // get centre point + radius
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    rad = configServer.GetStoredString("radius").toDouble();

    // The zoom level is a whole number, so the radius gets snapped to it. Using
    // the configured value from here on would put the aircraft next to the map.
    viewport = MakeViewport(lat, lon, rad);
    rad = viewport.effectiveRadius;

    Serial.printf("[AIRCRAFT] zoom %d, effective radius %.4f, box %.5f..%.5f / %.5f..%.5f\n",
                  viewport.zoom,
                  viewport.effectiveRadius,
                  viewport.LatMin(),
                  viewport.LatMax(),
                  viewport.LonMin(),
                  viewport.LonMax());

    // configuration. An unset key keeps the member default, so a device that
    // was never configured still shows everything.
    auto ReadToggle = [this](const char *key, bool &target)
    {
        const String stored = configServer.GetStoredString(key);
        if (!stored.isEmpty())
            target = stored == "true";
    };

    ReadToggle("infotext", displayInfoText);
    ReadToggle("triangle", displayTriangles);
    ReadToggle("circles", displayRadarCircles);
    ReadToggle("det-icon", detailIcon);
    ReadToggle("det-type", detailType);
    ReadToggle("det-route", detailRoute);
    ReadToggle("det-cities", detailAirportNames);
    ReadToggle("det-alt", detailAltitude);
    ReadToggle("det-vs", detailVerticalSpeed);
    ReadToggle("det-spd", detailSpeed);
    ReadToggle("det-spd-kmh", detailSpeedKmh);
    ReadToggle("det-hdg", detailHeading);
    ReadToggle("det-reg", detailRegistration);
    ReadToggle("det-callsign", detailCallsign);
    ReadToggle("det-icao", detailIcao);

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    const bool authenticated = !token.isEmpty();

    long intervalSeconds = configServer.GetStoredString("fetch-interval").toInt();

    // Zero is the automatic setting, spreading the day's credits evenly. It is
    // also what an unconfigured device reads, so the interval that was hardcoded
    // before this became a setting is still the one it starts on.
    if (intervalSeconds <= 0)
        intervalSeconds = OpenSkyBudget::AutomaticIntervalSeconds(authenticated);

    // A faster refresh than the allowance covers is the user's call to make: a
    // device that runs for four hours a day can afford one the clock could not.
    // Only the floor is enforced, since below it the request is simply wasted.
    intervalSeconds = constrain(
        intervalSeconds,
        OpenSkyBudget::MinimumIntervalSeconds,
        OpenSkyBudget::LargestIntervalSeconds);

    fetchInterval = (unsigned long)intervalSeconds * 1000UL;

    Serial.printf("[AIRCRAFT] %s, refresh every %ld s, %ld credits per day of uptime out of %ld\n",
                  authenticated ? "authenticated" : "anonymous",
                  intervalSeconds,
                  OpenSkyBudget::SecondsPerDay / intervalSeconds,
                  OpenSkyBudget::Credits(authenticated));
}

bool AircraftManager::NeedsAircraftInfo() const
{
    return detailType || detailRoute || detailAirportNames || detailRegistration;
}

void AircraftManager::RequestInfoLookup()
{
    infoLookupPending = true;
    infoLookupRequestedAt = millis();
}

void AircraftManager::ResolveSelectedAircraftInfo(unsigned long now)
{
    if (!infoLookupPending)
        return;

    // None of its rows are on screen, so the answer would never be read.
    if (!NeedsAircraftInfo())
    {
        infoLookupPending = false;
        return;
    }

    // Spinning the encoder walks past aircraft nobody wanted to look at. Waiting
    // for the selection to settle turns that into a single request.
    constexpr unsigned long SETTLE_MS = 250;
    if (now - infoLookupRequestedAt < SETTLE_MS)
        return;

    infoLookupPending = false;

    if (currentScreen != SCREEN_DETAILS ||
        visibleAircraft.empty() ||
        selectedAircraftIndex >= (int)visibleAircraft.size())
        return;

    const TrackedAircraft &tracked = *visibleAircraft[selectedAircraftIndex];

    infoProvider.Fetch(tracked.state.icao24, tracked.state.callsign);
}

void AircraftManager::Update()
{
    unsigned long now = millis();

    ResolveSelectedAircraftInfo(now);

    // fetch cycle
    if (now - lastFetch >= fetchInterval)
    {
        lastFetch = now;

        // auth
        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret"));

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty())
            headers.push_back({"Authorization", "Bearer " + token});

        // request
        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            // Exactly the visible viewport. Under Mercator that box is wider in
            // longitude than in latitude, so the previous square box in degrees
            // missed aircraft at the left and right edge.
            {{"lamin", String(viewport.LatMin(), 6)},
             {"lamax", String(viewport.LatMax(), 6)},
             {"lomin", String(viewport.LonMin(), 6)},
             {"lomax", String(viewport.LonMax(), 6)}},
            headers);

        if (!result.rateLimitRemaining.isEmpty())
        {
            configServer.SetOpenSkyRateLimitRemaining(result.rateLimitRemaining.toInt());
            Serial.printf("[AIRCRAFT] OpenSky X-Rate-Limit-Remaining: %s\n",
                          result.rateLimitRemaining.c_str());
        }

        // If request failed, skip this update
        if (!result.success)
        {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        // track
        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis(); // override with post-parse timestamp

        for (auto &ac : aircraft)
        {
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ac, now});
            else
                it->second.Update(ac, now);
        }

        // remove any planes that disappeared from the feed
        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end();)
        {
            bool aircraftPresent = std::any_of(aircraft.begin(), aircraft.end(), [&](const Aircraft &ac)
                                               { return ac.icao24 == it->first; });
            if (!aircraftPresent)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }
    }
}

void AircraftManager::DrawDetails(LGFX_Sprite &backbuffer)
{
    backbuffer.fillScreen(TFT_BLACK);

    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 5;

    // Outer circular frame
    backbuffer.drawCircle(
        CENTRE,
        CENTRE,
        OUTER,
        lgfx::color888(0, 200, 0));

    if (visibleAircraft.empty())
    {
        backbuffer.setTextColor(lgfx::color888(0, 255, 0));
        backbuffer.setTextDatum(textdatum_t::middle_center);
        backbuffer.drawString("NO AIRCRAFT", CENTRE, CENTRE);
        return;
    }

    TrackedAircraft &tracked =
        *visibleAircraft[selectedAircraftIndex];

    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    backbuffer.setTextDatum(textdatum_t::middle_center);

    // Every field is optional, so the rows stack downwards from here instead of
    // sitting at fixed offsets.
    int y = 45;
    constexpr int LINE = 20;
    constexpr int HEADING_LINE = 16;
    constexpr int BLOCK = 26;

    if (detailIcon)
    {
        DrawAircraftTriangle(
            backbuffer,
            CENTRE,
            y,
            tracked,
            false);

        y += BLOCK;
    }

    const AircraftInfo *info = nullptr;

    if (NeedsAircraftInfo())
    {
        info = infoProvider.TryGet(tracked.state.icao24);

        // The selection can also change without the encoder, when the aircraft
        // ahead of it drops out of the feed. Asking again here keeps the rows
        // from being stuck on LOADING, and the pending check stops the settle
        // timer from being pushed back on every frame.
        if (info == nullptr && !infoLookupPending)
            RequestInfoLookup();
    }

    // An airframe ADSBdb does not know leaves the operator as the only thing
    // worth heading the screen with, and the route row then has to give way to
    // avoid printing that same name twice.
    bool airlineAsHeading = false;

    if (detailType)
    {
        String type = "LOADING...";

        if (info != nullptr)
        {
            type = info->TypeLabel();

            if (type.isEmpty())
            {
                type = info->OperatorLabel();
                airlineAsHeading = !type.isEmpty();
            }

            if (type.isEmpty())
                type = "UNKNOWN";
        }

        backbuffer.setTextSize(2);

        // Size 2 fits about 14 characters inside the circle. A name that runs
        // longer just continues on the next line, split at a space if there is
        // one near the middle.
        constexpr unsigned int HeadingChars = 14;

        if (type.length() > HeadingChars)
        {
            int split = type.length() / 2;
            const int space = type.lastIndexOf(' ', split);

            if (space > 0)
                split = space;

            String line2 = type.substring(split);
            line2.trim();

            backbuffer.drawString(type.substring(0, split), CENTRE, y);
            backbuffer.drawString(line2, CENTRE, y + HEADING_LINE);
            y += BLOCK + HEADING_LINE;
        }
        else
        {
            backbuffer.drawString(type, CENTRE, y);
            y += BLOCK;
        }
    }

    backbuffer.setTextSize(1);

    if (detailRoute)
    {
        String route = "LOADING...";

        if (info != nullptr)
        {
            // RouteLabel names the operator where no route is known, which is
            // where the heading may already carry it.
            const bool repeatsHeading = airlineAsHeading && !info->HasRoute();

            route = repeatsHeading ? "" : info->RouteLabel();

            if (route.isEmpty())
                route = "NO ROUTE";
        }

        backbuffer.drawString(
            route,
            CENTRE,
            y);

        y += LINE;
    }

    if (detailAirportNames)
    {
        String names;

        if (info == nullptr)
        {
            if (!detailRoute)
                names = "LOADING...";
        }
        else
        {
            names = info->AirportNamesLabel();
        }

        if (!names.isEmpty())
        {
            backbuffer.drawString(
                names,
                CENTRE,
                y);

            y += LINE;
        }
    }

    if (detailAltitude)
    {
        backbuffer.drawString(
            "ALT " + String((int)tracked.state.baroAltitude) + " m",
            CENTRE,
            y);

        y += LINE;
    }

    if (detailVerticalSpeed)
    {
        const int rate = (int)tracked.state.verticalRate;

        // Whether the aircraft overhead has just left or is on approach is the
        // one thing worth a colour of its own, since it is readable from across
        // the room without the number.
        constexpr int LEVEL_FLIGHT = 1;

        if (rate >= LEVEL_FLIGHT)
            backbuffer.setTextColor(lgfx::color888(255, 190, 0));
        else if (rate <= -LEVEL_FLIGHT)
            backbuffer.setTextColor(lgfx::color888(0, 220, 255));

        String rateLabel = "V/S ";

        if (rate > 0)
            rateLabel += "+";

        rateLabel += String(rate) + " m/s";

        backbuffer.drawString(rateLabel, CENTRE, y);

        backbuffer.setTextColor(lgfx::color888(0, 255, 0));

        y += LINE;
    }

    if (detailSpeed)
    {
        backbuffer.drawString(
            "SPD " + String((int)tracked.state.velocity) + " m/s",
            CENTRE,
            y);

        y += LINE;
    }

    if (detailSpeedKmh)
    {
        backbuffer.drawString(
            "SPD " + String((int)(tracked.state.velocity * 3.6f + 0.5f)) + " km/h",
            CENTRE,
            y);

        y += LINE;
    }

    if (detailHeading)
    {
        backbuffer.drawString(
            "HDG " + String((int)tracked.state.trueTrack) + " deg",
            CENTRE,
            y);

        y += LINE;
    }

    if (detailRegistration)
    {
        String registration = "LOADING...";

        if (info != nullptr)
            registration = info->registration.isEmpty()
                               ? "NO REG"
                               : "REG " + info->registration;

        backbuffer.drawString(registration, CENTRE, y);

        y += LINE;
    }

    if (detailCallsign)
    {
        String callsign = tracked.state.callsign;
        callsign.trim();

        backbuffer.drawString(
            callsign,
            CENTRE,
            y);

        y += LINE;
    }

    if (detailIcao)
    {
        backbuffer.drawString(
            "XPDR " + tracked.state.icao24,
            CENTRE,
            y);

        y += LINE;
    }
}

void AircraftManager::EncoderClick()
{
    currentScreen =
        (currentScreen == SCREEN_RADAR)
            ? SCREEN_DETAILS
            : SCREEN_RADAR;

    if (currentScreen == SCREEN_DETAILS)
        RequestInfoLookup();
}

void AircraftManager::Draw(LGFX_Sprite &backbuffer)
{
    visibleAircraft.clear();

    for (auto &[icao, tracked] : trackedAircraft)
    {
        if (tracked.state.onGround)
            continue;

        visibleAircraft.push_back(&tracked);
    }

    if (currentScreen == SCREEN_DETAILS)
    {
        DrawDetails(backbuffer);
        return;
    }

    if (displayRadarCircles)
        DrawRadarCircles(backbuffer);

    visibleAircraft.clear();

    for (auto &[icao, tracked] : trackedAircraft)
    {
        if (tracked.state.onGround)
            continue;

        visibleAircraft.push_back(&tracked);
    }

    if (!visibleAircraft.empty())
    {
        selectedAircraftIndex =
            constrain(
                selectedAircraftIndex,
                0,
                (int)visibleAircraft.size() - 1);
    }
    else
    {
        selectedAircraftIndex = 0;
    }

    for (size_t i = 0; i < visibleAircraft.size(); i++)
    {
        TrackedAircraft &tracked = *visibleAircraft[i];

        tracked.Tick();

        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

        bool selected =
            ((int)i == selectedAircraftIndex);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked);

        if (displayTriangles)
            DrawAircraftTriangle(
                backbuffer,
                x,
                y,
                tracked,
                selected);
        else
        {
            backbuffer.fillCircle(
                x,
                y,
                selected ? 5 : 3,
                selected
                    ? lgfx::color888(0, 0, 255)
                    : lgfx::color888(0, 255, 0));
        }
    }
}

uint32_t AircraftManager::GetAircraftColour(
    const TrackedAircraft &tracked) const
{
    uint32_t hash = 0;

    for (char c : tracked.state.icao24)
        hash = hash * 31 + c;

    return AircraftColours[hash % (sizeof(AircraftColours) /
                                   sizeof(AircraftColours[0]))];
}

void AircraftManager::SelectNextAircraft()
{
    if (visibleAircraft.empty())
        return;

    selectedAircraftIndex++;

    if (selectedAircraftIndex >= visibleAircraft.size())
        selectedAircraftIndex = 0;

    if (currentScreen == SCREEN_DETAILS)
        RequestInfoLookup();

    Serial.print("Aircraft count: ");
    Serial.println(visibleAircraft.size());

    Serial.print("Selected index: ");
    Serial.println(selectedAircraftIndex);
}

void AircraftManager::SelectPreviousAircraft()
{
    if (visibleAircraft.empty())
        return;

    selectedAircraftIndex--;

    if (selectedAircraftIndex < 0)
        selectedAircraftIndex =
            visibleAircraft.size() - 1;

    if (currentScreen == SCREEN_DETAILS)
        RequestInfoLookup();

    Serial.print("Selected previous aircraft, index: ");
    Serial.println(selectedAircraftIndex);
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite &backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 5;

    // Brighter than before: these no longer sit on black but on a map, where
    // the old 64 and 32 green were practically invisible.
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER, lgfx::color888(0, 220, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, (OUTER / 3) * 2, lgfx::color888(0, 110, 0));
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER / 3, lgfx::color888(0, 80, 0));
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    // Web Mercator against the same origin the map tiles were placed at, which
    // makes map and aircraft line up by construction. The previous equidistant
    // formula stretched the picture horizontally by 1 / cos(lat) - at 52 degrees
    // north that is 62 percent.
    const double x = WebMercator::LonToWorldPx(predLon, viewport.zoom) - viewport.originX;
    const double y = WebMercator::LatToWorldPx(predLat, viewport.zoom) - viewport.originY;

    return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const
{
    // const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 128, 0));
    backbuffer.drawString(tracked.state.callsign, x + 10, y + 15);
    // backbuffer.drawString(String(tracked.state.velocity) + "m/s", x + 5, y + 5 + lineHeight);
    // backbuffer.drawString(String(tracked.state.baroAltitude) + "m", x + 5, y + 5 + lineHeight * 2);
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite &backbuffer, int x, int y, const TrackedAircraft &tracked) const
{
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));
    const float px = -dy;
    const float py = dx;

    constexpr float TRIANGLE_LENGTH = 6.0f;
    constexpr float TRIANGLE_WIDTH = 3.0f;

    const float tipX = x + dx * TRIANGLE_LENGTH;
    const float tipY = y + dy * TRIANGLE_LENGTH;
    const float leftX = x - dx * TRIANGLE_LENGTH * 0.5f + px * TRIANGLE_WIDTH * 0.5f;
    const float leftY = y - dy * TRIANGLE_LENGTH * 0.5f + py * TRIANGLE_WIDTH * 0.5f;
    const float rightX = x - dx * TRIANGLE_LENGTH * 0.5f - px * TRIANGLE_WIDTH * 0.5f;
    const float rightY = y - dy * TRIANGLE_LENGTH * 0.5f - py * TRIANGLE_WIDTH * 0.5f;

    backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, lgfx::color888(0, 255, 0));
}

void AircraftManager::DrawAircraftTriangle(
    LGFX_Sprite &backbuffer,
    int x,
    int y,
    const TrackedAircraft &tracked,
    bool selected) const
{
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));

    const float px = -dy;
    const float py = dx;

    // uint32_t, because LovyanGFX reads a 16-bit argument as a packed RGB565
    // value: truncating here turned every colour with a red component into
    // something else entirely.
    const uint32_t colour =
        selected
            ? SelectionColour
            : GetAircraftColour(tracked);

    if (selected)
        backbuffer.drawCircle(x, y, SelectionRingRadius, colour);

    constexpr float BODY_FRONT = 8.0f;
    constexpr float BODY_REAR = 6.0f;

    constexpr float WING_SPAN = 8.0f;
    constexpr float WING_SWEEP = 4.0f;

    // Fuselage endpoints
    const float noseX = x + dx * BODY_FRONT;
    const float noseY = y + dy * BODY_FRONT;

    const float tailX = x - dx * BODY_REAR;
    const float tailY = y - dy * BODY_REAR;

    // Rounded fuselage (implemented as 3 parallel lines)
    for (int i = -1; i <= 1; i++)
    {
        backbuffer.drawLine(
            noseX + px * i,
            noseY + py * i,
            tailX + px * i,
            tailY + py * i,
            colour);
    }

    // Rounded ends
    backbuffer.fillCircle(noseX, noseY, 1, colour);
    backbuffer.fillCircle(tailX, tailY, 1, colour);

    // Wing triangle
    const float wingCenterX = x + dx * 2.5f;
    const float wingCenterY = y + dy * 2.5f;

    const float leftWingX =
        wingCenterX + px * WING_SPAN - dx * WING_SWEEP;

    const float leftWingY =
        wingCenterY + py * WING_SPAN - dy * WING_SWEEP;

    const float rightWingX =
        wingCenterX - px * WING_SPAN - dx * WING_SWEEP;

    const float rightWingY =
        wingCenterY - py * WING_SPAN - dy * WING_SWEEP;

    backbuffer.fillTriangle(
        wingCenterX,
        wingCenterY,

        leftWingX,
        leftWingY,

        rightWingX,
        rightWingY,

        colour);
}