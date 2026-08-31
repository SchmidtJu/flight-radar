#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>

#include "HttpRequestManager.h"
#include "models/AircraftInfo.h"

// Resolves aircraft type and route on demand, one aircraft at a time.
//
// This runs against ADSBdb rather than OpenSky: the state vector feed carries
// neither field, and ADSBdb answers both without an API key, so it costs none of
// the OpenSky request budget. Airframes it does not hold are looked up a second
// time at hexdb.io.
//
// Only the aircraft selected on the details screen is ever looked up, and the
// answer is cached for as long as the device runs.
class AircraftInfoProvider
{
private:
    // A miss is stored as an empty AircraftInfo, so presence in the map alone
    // says whether a lookup already happened. Without that, the aircraft ADSBdb
    // does not know - roughly a third of the traffic - would be requested again
    // on every turn of the encoder.
    std::map<String, AircraftInfo> cache;

    // ICAO airline code to name, an empty name meaning "asked, nothing there".
    // Keyed by airline rather than by aircraft, so one lookup serves every
    // machine of that operator. There are only a few hundred airlines over any
    // one viewport, so this needs no size limit.
    std::map<String, String> airlineCache;

    HttpRequestManager &http;

    // Aircraft leave the viewport on their own, so the cache is bounded only to
    // stop a device that runs for weeks from growing without limit.
    static constexpr size_t CacheLimit = 100;

    // GET plus status check plus parse. False for anything other than a parsed
    // 200, which is the only case a caller can use.
    bool FetchJson(const String &url, JsonDocument &doc);

    // Operator behind a commercial callsign, empty for anything else.
    String ResolveAirline(const String &callsign);

    // Second opinion on the airframe, for the aircraft ADSBdb has no entry for.
    // Fills type, registration and owner where it finds them and leaves info
    // untouched otherwise. Its own API, so its own parser rather than the
    // shared one.
    void ResolveAirframeFallback(const String &hex, AircraftInfo &info);

public:
    explicit AircraftInfoProvider(HttpRequestManager &httpManager)
        : http(httpManager)
    {
    }
    ~AircraftInfoProvider() = default;

    // True once a lookup finished, whether it found anything or not.
    bool IsResolved(const String &icao24) const;

    // Cache only, never the network. nullptr means "not looked up yet".
    const AircraftInfo *TryGet(const String &icao24) const;

    // Blocks for the duration of one to three HTTP requests. Does nothing if
    // the aircraft is already resolved.
    void Fetch(const String &icao24, const String &callsign);
};
