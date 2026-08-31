#include "ConfigurationWebServer.h"
#include "OpenSkyBudget.h"
#include "Viewport.h"
#include <ESPmDNS.h>

// HTML stored in flash
// %PLACEHOLDER% tokens are substituted at serve time by the template processor
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Micro Radar</title>
        <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4.3.0"></script>
    </head>
    <body class="font-mono bg-gray-900 text-green-500 min-h-screen p-4 sm:p-0 text-md sm:text-sm">
        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10">
            <legend class="px-2">Configure Micro Radar</legend>

            <form id="cfg" action="/save" method="POST" class="flex flex-col gap-4 sm:gap-2">

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Latitude:</span>
                        <input
                            name="latitude"
                            type="number"
                            min="-90"
                            step="0.000001"
                            max="90"
                            value='%LATITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>

                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Longitude:</span>
                        <input
                            name="longitude"
                            type="number"
                            min="-180"
                            step="0.000001"
                            max="180"
                            value='%LONGITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                </div>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Radius (in &deg;):</span>
                    <input
                        name="radius"
                        type="number"
                        min="0.000001"
                        step="0.000001"
                        max="2.499999"
                        value='%RADIUS%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <div class="text-xs opacity-80">%ZOOM_INFO%</div>

                <fieldset class="border border-green-500 p-3 flex flex-col gap-4 sm:gap-2">
                    <legend class="px-1">Map</legend>

                    <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                        <label class="flex items-center gap-2">
                            <span>Background:</span>
                            <input name="map" type="checkbox" %MAP% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Dark filter:</span>
                            <input name="map-dark" type="checkbox" %MAP_DARK% class="accent-green-500">
                        </label>
                    </div>

                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Brightness (percent):</span>
                        <input
                            name="map-brightness"
                            type="range"
                            min="5"
                            max="100"
                            step="5"
                            value='%MAP_BRIGHTNESS%'
                            oninput="this.nextElementSibling.textContent = this.value"
                            class="flex-1 w-full accent-green-500">
                        <span class="w-8 text-right">%MAP_BRIGHTNESS%</span>
                    </label>

                    <button
                        type="button"
                        id="reload"
                        class="border border-green-500 px-3 py-2 sm:px-2 sm:py-0 self-start cursor-pointer">
                        Reload map now
                    </button>
                </fieldset>

                <fieldset class="border border-green-500 p-3 flex flex-col gap-4 sm:gap-2">
                    <legend class="px-1">Display</legend>

                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Backlight (percent):</span>
                        <input
                            name="backlight"
                            type="range"
                            min="5"
                            max="100"
                            step="5"
                            value='%BACKLIGHT%'
                            oninput="this.nextElementSibling.textContent = this.value"
                            class="flex-1 w-full accent-green-500">
                        <span class="w-8 text-right">%BACKLIGHT%</span>
                    </label>

                    <label class="flex items-center gap-2">
                        <span>Rotate by 180 degrees:</span>
                        <input name="flip" type="checkbox" %FLIP% class="accent-green-500">
                    </label>
                </fieldset>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>OpenSkyAPI Client ID:</span>
                    <input
                        name="opensky-id"
                        value='%OPENSKY_ID%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>OpenSkyAPI Client Secret:</span>
                    <input
                        name="opensky-secret"
                        value='%OPENSKY_SECRET%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <fieldset class="border border-green-500 p-3 flex flex-col gap-4 sm:gap-2">
                    <legend class="px-1">Aircraft refresh</legend>

                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Every (seconds, 0 for automatic):</span>
                        <input
                            name="fetch-interval"
                            id="fetch-interval"
                            type="number"
                            min="0"
                            max='%INTERVAL_MAX%'
                            step="1"
                            value='%FETCH_INTERVAL%'
                            class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>

                    <p id="credits" class="text-green-700"></p>

                    <p id="rate-limit" class="text-green-700">X-Rate-Limit-Remaining: %RATE_LIMIT_REMAINING%</p>
                </fieldset>

                <div class="grid grid-cols-1 sm:grid-cols-2 gap-4 sm:gap-2">
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Radar sweep:</span>
                        <input
                            name="scanline"
                            type="checkbox"
                            %SCANLINE%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Radar circles:</span>
                        <input
                            name="circles"
                            type="checkbox"
                            %CIRCLES%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Aircraft Info:</span>
                        <input
                            name="infotext"
                            type="checkbox"
                            %INFOTEXT%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Directional Aircraft:</span>
                        <input
                            name="triangle"
                            type="checkbox"
                            %TRIANGLE%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                </div>

                <fieldset class="border border-green-500 p-3 flex flex-col gap-4 sm:gap-2">
                    <legend class="px-1">Aircraft details</legend>

                    <div class="grid grid-cols-1 sm:grid-cols-3 gap-4 sm:gap-2">
                        <label class="flex items-center gap-2">
                            <span>Icon:</span>
                            <input name="det-icon" type="checkbox" %DET_ICON% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Type:</span>
                            <input name="det-type" type="checkbox" %DET_TYPE% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Route:</span>
                            <input name="det-route" type="checkbox" %DET_ROUTE% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Airport names:</span>
                            <input name="det-cities" type="checkbox" %DET_CITIES% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Altitude:</span>
                            <input name="det-alt" type="checkbox" %DET_ALT% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Climb rate:</span>
                            <input name="det-vs" type="checkbox" %DET_VS% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Speed (m/s):</span>
                            <input name="det-spd" type="checkbox" %DET_SPD% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Speed (km/h):</span>
                            <input name="det-spd-kmh" type="checkbox" %DET_SPD_KMH% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Heading:</span>
                            <input name="det-hdg" type="checkbox" %DET_HDG% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Registration:</span>
                            <input name="det-reg" type="checkbox" %DET_REG% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Callsign:</span>
                            <input name="det-callsign" type="checkbox" %DET_CALLSIGN% class="accent-green-500">
                        </label>
                        <label class="flex items-center gap-2">
                            <span>Transponder:</span>
                            <input name="det-icao" type="checkbox" %DET_ICAO% class="accent-green-500">
                        </label>
                    </div>
                </fieldset>

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <input
                        type="submit"
                        value="Save"
                        class="bg-green-500 text-black mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-0 self-start cursor-pointer">

                        <div id="result" class="mt-4 px-1 sm:px-10"></div>
                </div>
            </form>

            <div class="border-t border-green-500 mt-5 pt-2 text-xs">
                Map data (c)
                <a href="https://www.openstreetmap.org/copyright" class="underline">OpenStreetMap contributors</a>,
                tiles from tile.openstreetmap.org.
                <a href="https://www.openstreetmap.org/fixthemap" class="underline">Report a map issue</a>.
            </div>
        </fieldset>

        <script>
            document.getElementById('cfg').addEventListener('submit', function(e) {
                e.preventDefault();
                fetch(this.action, { method: 'POST', body: new FormData(this) })
                    .then(r => r.text())
                    .then(html => document.getElementById('result').innerHTML = html);
            });

            document.getElementById('reload').addEventListener('click', function() {
                fetch('/reload-map', { method: 'POST' })
                    .then(r => r.text())
                    .then(html => document.getElementById('result').innerHTML = html);
            });

            const budget = %CREDIT_BUDGET%;
            const authenticated = %CREDIT_AUTHED%;
            const floorSeconds = %INTERVAL_MIN%;
            const interval = document.getElementById('fetch-interval');
            const credits = document.getElementById('credits');

            function showCredits() {
                const wanted = parseInt(interval.value, 10) || 0;
                const automatic = wanted <= 0;
                const seconds = automatic
                    ? Math.ceil(86400 / budget)
                    : Math.max(wanted, floorSeconds);
                const perDay = Math.ceil(86400 / seconds);

                let text = 'One request every ' + seconds + ' s, so ' + perDay
                    + ' credits per 24 hours of uptime, against an allowance of ' + budget + '.';

                if (automatic) {
                    text += ' Automatic spreads the allowance over a full day.';
                }
                if (perDay > budget) {
                    text += ' That runs out after ' + (24 * budget / perDay).toFixed(1) + ' hours.';
                }
                if (!authenticated) {
                    text += ' Fill in the client ID and secret above for ten times the allowance.';
                }

                credits.textContent = text;
                credits.className = perDay > budget ? 'text-amber-400' : 'text-green-700';
            }

            interval.addEventListener('input', showCredits);
            showCredits();

            const rateLimit = document.getElementById('rate-limit');

            function showRateLimit(value) {
                const unknown = !value || value === 'unknown';
                const remaining = parseInt(value, 10);
                rateLimit.textContent = 'X-Rate-Limit-Remaining: ' + (unknown ? 'unknown' : value);
                rateLimit.className = !unknown && remaining === 0 ? 'text-amber-400' : 'text-green-700';
            }

            function refreshRateLimit() {
                fetch('/opensky-remaining')
                    .then(r => r.text())
                    .then(showRateLimit)
                    .catch(function() {});
            }

            setInterval(refreshRateLimit, 5000);
            refreshRateLimit();
        </script>
    </body>
</html>
)";

void ConfigurationWebServer::Initialise()
{
    // start mDNS and check result
    if (!MDNS.begin("microradar"))
    {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    // Handle visit to config web server
    server.on("/", HTTP_GET, [&](AsyncWebServerRequest *request)
              {
        Serial.println("[GET] Handling request to config web server...");

        // read all values up front so the processor lambda can capture by value
        prefs.begin("config", true);
        const String latitude = prefs.getString("latitude", "");
        const String longitude = prefs.getString("longitude", "");
        const String radius = prefs.getString("radius", "1.0");
        const String openskyClientId = prefs.getString("opensky-id", "");
        String openskySecret = prefs.getString("opensky-secret", "");
        const String scanlineEnabled = prefs.getString("scanline", "true");
        const String circlesEnabled = prefs.getString("circles", "true");
        const String infoTextEnabled = prefs.getString("infotext", "true");
        const String triangleEnabled = prefs.getString("triangle", "true");
        const String detailIcon = prefs.getString("det-icon", "true");
        const String detailType = prefs.getString("det-type", "true");
        const String detailRoute = prefs.getString("det-route", "true");
        const String detailAirportNames = prefs.getString("det-cities", "true");
        const String detailAltitude = prefs.getString("det-alt", "true");
        const String detailVerticalSpeed = prefs.getString("det-vs", "true");
        const String detailSpeed = prefs.getString("det-spd", "true");
        const String detailSpeedKmh = prefs.getString("det-spd-kmh", "true");
        const String detailHeading = prefs.getString("det-hdg", "true");
        const String detailRegistration = prefs.getString("det-reg", "true");
        const String detailCallsign = prefs.getString("det-callsign", "true");
        const String detailIcao = prefs.getString("det-icao", "true");
        const String mapEnabled = prefs.getString("map", "true");
        const String darkFilterEnabled = prefs.getString("map-dark", "true");
        const String mapBrightness = prefs.getString("map-brightness", "100");
        const String backlight = prefs.getString("backlight", "100");
        const String flipped = prefs.getString("flip", "false");
        const String fetchInterval = prefs.getString("fetch-interval", "0");
        prefs.end();

        // Whether the credentials are there, not whether they work. The panel
        // only needs it to name the allowance the firmware will end up using.
        const bool authenticated = !openskyClientId.isEmpty() && !openskySecret.isEmpty();
        const String creditBudget = String(OpenSkyBudget::Credits(authenticated));

        // The zoom level is a whole number, so the configured radius gets
        // snapped to it. Showing the result makes that visible instead of
        // leaving the user guessing why the picture does not match the input.
        const Viewport viewport = MakeViewport(latitude.toDouble(), longitude.toDouble(), radius.toDouble());

        const String zoomInfo = String("Zoom ") + viewport.zoom
                                + ", effective radius " + String(viewport.effectiveRadius, 4)
                                + " deg, about " + String(viewport.effectiveRadius * 2.0 * 111.32, 0)
                                + " km across";

        // mask secret before sending to client
        std::fill(openskySecret.begin(), openskySecret.end(), '*');

        const int remainingCredits = GetOpenSkyRateLimitRemaining();
        const String rateLimitRemaining = remainingCredits < 0 ? String("unknown") : String(remainingCredits);

        // template processor called once per %PLACEHOLDER% token found in CONFIG_HTML.
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [latitude, longitude, radius, openskyClientId, openskySecret, scanlineEnabled, circlesEnabled, infoTextEnabled, triangleEnabled,
             mapEnabled, darkFilterEnabled, mapBrightness, backlight, flipped, zoomInfo, fetchInterval, creditBudget, authenticated,
             rateLimitRemaining, detailIcon, detailType, detailRoute, detailAirportNames, detailAltitude, detailVerticalSpeed,
             detailSpeed, detailSpeedKmh, detailHeading, detailRegistration, detailCallsign, detailIcao]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return radius;
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "CIRCLES")        return circlesEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                if (var == "MAP")            return mapEnabled == "true" ? "checked" : "";
                if (var == "MAP_DARK")       return darkFilterEnabled == "true" ? "checked" : "";
                if (var == "MAP_BRIGHTNESS") return mapBrightness;
                if (var == "BACKLIGHT")      return backlight;
                if (var == "FLIP")           return flipped == "true" ? "checked" : "";
                if (var == "ZOOM_INFO")      return zoomInfo;
                if (var == "FETCH_INTERVAL") return fetchInterval;
                if (var == "CREDIT_BUDGET")  return creditBudget;
                if (var == "CREDIT_AUTHED")  return authenticated ? "true" : "false";
                if (var == "INTERVAL_MIN")   return String(OpenSkyBudget::MinimumIntervalSeconds);
                if (var == "INTERVAL_MAX")   return String(OpenSkyBudget::LargestIntervalSeconds);
                if (var == "RATE_LIMIT_REMAINING") return rateLimitRemaining;
                if (var == "DET_ICON")       return detailIcon == "true" ? "checked" : "";
                if (var == "DET_TYPE")       return detailType == "true" ? "checked" : "";
                if (var == "DET_ROUTE")      return detailRoute == "true" ? "checked" : "";
                if (var == "DET_CITIES")     return detailAirportNames == "true" ? "checked" : "";
                if (var == "DET_ALT")        return detailAltitude == "true" ? "checked" : "";
                if (var == "DET_VS")         return detailVerticalSpeed == "true" ? "checked" : "";
                if (var == "DET_SPD")        return detailSpeed == "true" ? "checked" : "";
                if (var == "DET_SPD_KMH")    return detailSpeedKmh == "true" ? "checked" : "";
                if (var == "DET_HDG")        return detailHeading == "true" ? "checked" : "";
                if (var == "DET_REG")        return detailRegistration == "true" ? "checked" : "";
                if (var == "DET_CALLSIGN")   return detailCallsign == "true" ? "checked" : "";
                if (var == "DET_ICAO")       return detailIcao == "true" ? "checked" : "";
                return "";
            }
        );
        request->send(response); });

    // Handle save submission to web server
    server.on("/save", HTTP_POST, [&](AsyncWebServerRequest *request)
              {
        Serial.println("[POST] Handling form submission to config web server...");

        // safe parameter retrieval helper lambda
        auto TrySaveParam = [request, this](const char* paramName) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr)
                return false;

            prefs.putString(paramName, param->value());
            return true;
            };

        prefs.begin("config", false);

        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("radius");
        TrySaveParam("opensky-id");
        TrySaveParam("map-brightness");
        TrySaveParam("backlight");
        TrySaveParam("fetch-interval");

        const auto* param = request->getParam("opensky-secret", true);
        if (param != nullptr) {
            const String& secret = param->value();
            if (secret.indexOf('*') == -1) { // Special handling for secret: don't overwrite with masked value
                prefs.putString("opensky-secret", secret);
            }
        }

        // An unchecked box sends no parameter at all, so absence means false.
        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
        prefs.putString("circles", request->hasParam("circles", true) ? "true" : "false");
        prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
        prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
        prefs.putString("map", request->hasParam("map", true) ? "true" : "false");
        prefs.putString("map-dark", request->hasParam("map-dark", true) ? "true" : "false");
        prefs.putString("flip", request->hasParam("flip", true) ? "true" : "false");
        prefs.putString("det-icon", request->hasParam("det-icon", true) ? "true" : "false");
        prefs.putString("det-type", request->hasParam("det-type", true) ? "true" : "false");
        prefs.putString("det-route", request->hasParam("det-route", true) ? "true" : "false");
        prefs.putString("det-cities", request->hasParam("det-cities", true) ? "true" : "false");
        prefs.putString("det-alt", request->hasParam("det-alt", true) ? "true" : "false");
        prefs.putString("det-vs", request->hasParam("det-vs", true) ? "true" : "false");
        prefs.putString("det-spd", request->hasParam("det-spd", true) ? "true" : "false");
        prefs.putString("det-spd-kmh", request->hasParam("det-spd-kmh", true) ? "true" : "false");
        prefs.putString("det-hdg", request->hasParam("det-hdg", true) ? "true" : "false");
        prefs.putString("det-reg", request->hasParam("det-reg", true) ? "true" : "false");
        prefs.putString("det-callsign", request->hasParam("det-callsign", true) ? "true" : "false");
        prefs.putString("det-icao", request->hasParam("det-icao", true) ? "true" : "false");
        prefs.end();

        request->send(200, "text/html", "Saved - restarting device...");
        ESP.restart(); });

    // Refetching means four TLS downloads, which must not happen inside an
    // async handler: it would block the server task for seconds. The flag is
    // picked up by the main loop instead.
    server.on("/reload-map", HTTP_POST, [&](AsyncWebServerRequest *request)
              {
        Serial.println("[POST] map reload requested");

        mapReloadRequested = true;

        request->send(200, "text/html", "Reloading map..."); });

    server.on("/opensky-remaining", HTTP_GET, [this](AsyncWebServerRequest *request)
              {
        const int remaining = GetOpenSkyRateLimitRemaining();
        request->send(200, "text/plain", remaining < 0 ? "unknown" : String(remaining)); });

    server.begin();
}

bool ConfigurationWebServer::ConsumeMapReloadRequest()
{
    if (!mapReloadRequested)
    {
        return false;
    }

    mapReloadRequested = false;

    return true;
}

void ConfigurationWebServer::SetOpenSkyRateLimitRemaining(int remaining)
{
    openSkyRateLimitRemaining = remaining;
}

int ConfigurationWebServer::GetOpenSkyRateLimitRemaining() const
{
    return openSkyRateLimitRemaining;
}

const String ConfigurationWebServer::GetStoredString(const char *key)
{
    if (!prefs.begin("config", false))
    {
        return "";
    }

    const String value = prefs.getString(key, "");
    prefs.end();
    return value;
}