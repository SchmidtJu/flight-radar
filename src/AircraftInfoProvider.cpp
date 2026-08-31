#include "AircraftInfoProvider.h"

namespace
{
    const char *ApiBase = "https://api.adsbdb.com/v0/";

    // ADSBdb takes its airframes from Planebase, which is missing a good part
    // of the registrations in the air - mostly recent deliveries. hexdb.io
    // draws on a different dataset and closes about half of those gaps.
    const char *TypeFallbackBase = "https://hexdb.io/api/v1/aircraft/";
}

bool AircraftInfoProvider::IsResolved(const String &icao24) const
{
    return cache.find(icao24) != cache.end();
}

const AircraftInfo *AircraftInfoProvider::TryGet(const String &icao24) const
{
    const auto it = cache.find(icao24);

    return it == cache.end() ? nullptr : &it->second;
}

bool AircraftInfoProvider::FetchJson(const String &url, JsonDocument &doc)
{
    const HttpResult result = http.Get(url);

    // HttpRequestManager::Get reports success for any HTTP status it got an
    // answer for, so anything the database does not have arrives here as a 404
    // with a JSON error body.
    if (!result.success || result.statusCode != 200)
        return false;

    const DeserializationError error = deserializeJson(doc, result.response);

    if (error)
    {
        Serial.printf("[WARN] Aircraft lookup did not parse: %s\n", error.c_str());
        return false;
    }

    return true;
}

String AircraftInfoProvider::ResolveAirline(const String &callsign)
{
    // A commercial callsign is three letters and then the flight number. Any
    // other shape is a registration, and sending the first three characters of
    // "DFIRE" off to the API would only waste a request.
    if (callsign.length() < 4 || !isDigit(callsign[3]))
        return "";

    for (int i = 0; i < 3; i++)
        if (!isAlpha(callsign[i]))
            return "";

    String code = callsign.substring(0, 3);
    code.toUpperCase();

    const auto cached = airlineCache.find(code);
    if (cached != airlineCache.end())
        return cached->second;

    String name = "";

    JsonDocument doc;
    if (FetchJson(ApiBase + String("airline/") + code, doc) &&
        !doc["response"][0]["name"].isNull())
    {
        name = AircraftText::AsciiOnly(doc["response"][0]["name"].as<String>());
    }

    airlineCache.emplace(code, name);

    return name;
}

void AircraftInfoProvider::ResolveAirframeFallback(const String &hex, AircraftInfo &info)
{
    JsonDocument doc;

    if (!FetchJson(TypeFallbackBase + hex, doc))
        return;

    // Its "Type" is the marketing name ("EMB-190 LR"), too long for the row,
    // so the designator is taken from ICAOTypeCode as with ADSBdb.
    if (!doc["Manufacturer"].isNull())
        info.manufacturer = AircraftText::AsciiOnly(doc["Manufacturer"].as<String>());

    if (!doc["ICAOTypeCode"].isNull())
        info.icaoType = AircraftText::AsciiOnly(doc["ICAOTypeCode"].as<String>());

    // An airframe missing from ADSBdb is missing whole, not field by field, so
    // these are as empty as the type was and cost nothing extra to fill here.
    if (info.registration.isEmpty() && !doc["Registration"].isNull())
        info.registration = AircraftText::AsciiOnly(doc["Registration"].as<String>());

    if (info.owner.isEmpty() && !doc["RegisteredOwners"].isNull())
        info.owner = AircraftText::AsciiOnly(doc["RegisteredOwners"].as<String>());
}

void AircraftInfoProvider::Fetch(const String &icao24, const String &callsign)
{
    if (icao24.isEmpty() || IsResolved(icao24))
        return;

    // Dropped wholesale rather than by age: refilling an entry costs a single
    // request, so tracking what to evict would be more expensive than the loss.
    if (cache.size() >= CacheLimit)
        cache.clear();

    String hex = icao24;
    hex.toUpperCase();

    // OpenSky pads the callsign with spaces, and it is null for aircraft that
    // broadcast no identification at all. Without one the route cannot be
    // resolved, but the type still can.
    String flight = callsign;
    flight.trim();

    AircraftInfo info;

    // The combined endpoint covers both halves in one request, but only when
    // ADSBdb holds both. It answers 404 as soon as either the airframe or the
    // callsign is missing, and does not say which - so the fallback asks for
    // the two separately. Both endpoints nest their payload identically, which
    // is why the same parser reads either one.
    bool resolved = false;

    if (!flight.isEmpty())
    {
        JsonDocument doc;

        if (FetchJson(ApiBase + String("aircraft/") + hex + "?callsign=" + flight, doc))
        {
            JsonVariant response = doc["response"];
            info = JsonParser::Parse<AircraftInfo>(response);
            resolved = true;
        }
    }

    if (!resolved)
    {
        JsonDocument aircraftDoc;

        if (FetchJson(ApiBase + String("aircraft/") + hex, aircraftDoc))
        {
            JsonVariant response = aircraftDoc["response"];
            info = JsonParser::Parse<AircraftInfo>(response);
        }

        if (!flight.isEmpty())
        {
            JsonDocument routeDoc;

            if (FetchJson(ApiBase + String("callsign/") + flight, routeDoc))
            {
                JsonVariant response = routeDoc["response"];
                const AircraftInfo route = JsonParser::Parse<AircraftInfo>(response);

                info.originIata = route.originIata;
                info.destinationIata = route.destinationIata;
                info.originCity = route.originCity;
                info.destinationCity = route.destinationCity;
                info.airline = route.airline;
            }
        }
    }

    if (info.TypeLabel().isEmpty())
        ResolveAirframeFallback(hex, info);

    // Modern airline callsigns carry a random suffix that a callsign-keyed
    // route database cannot resolve. Naming the operator still beats an empty
    // row, and the airline cache makes it free from the second aircraft of that
    // carrier onwards.
    if (!info.HasRoute() && info.airline.isEmpty())
        info.airline = ResolveAirline(flight);

    Serial.printf("[INFO] Aircraft %s '%s': type '%s', reg '%s', route '%s', airports '%s'\n",
                  hex.c_str(),
                  flight.c_str(),
                  info.TypeLabel().c_str(),
                  info.registration.c_str(),
                  info.RouteLabel().c_str(),
                  info.AirportNamesLabel().c_str());

    cache.emplace(icao24, info);
}
