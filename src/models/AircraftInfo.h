#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "JsonParser.h"

// Maps to the ADSBdb /v0/aircraft/{icao24}?callsign={callsign} response, which
// fills the gaps OpenSky leaves: its state vectors carry neither the aircraft
// type nor the route.
//
// Every field is optional. An aircraft may be missing from the database, and a
// route only exists for callsigns that belong to a scheduled flight, so an
// empty string is the normal case rather than an error.
struct AircraftInfo
{
    String manufacturer;    // e.g. "Airbus"
    String icaoType;        // ICAO type designator, e.g. "A321"
    String registration;    // tail number, e.g. "D-AIMA"
    String owner;           // registered owner, e.g. "Lufthansa"
    String originIata;      // departure airport, e.g. "FRA"
    String destinationIata; // arrival airport, e.g. "LHR"
    String originCity;      // town the departure airport serves, e.g. "Frankfurt"
    String destinationCity; // town the arrival airport serves, e.g. "London"
    String airline;         // e.g. "Eurowings"

    // "Airbus A321". Either half can be missing, so the label falls back to
    // whichever one the database knew, and is empty when it knew neither.
    String TypeLabel() const
    {
        if (manufacturer.isEmpty())
            return icaoType;

        if (icaoType.isEmpty())
            return manufacturer;

        return manufacturer + " " + icaoType;
    }

    bool HasRoute() const
    {
        return !originIata.isEmpty() && !destinationIata.isEmpty();
    }

    bool HasAirportNames() const
    {
        return !originCity.isEmpty() && !destinationCity.isEmpty();
    }

    // Who to name where no route is known. An aircraft outside airline service
    // has no operator on the route, and there the owner is the whole point.
    String OperatorLabel() const
    {
        return airline.isEmpty() ? owner : airline;
    }

    // "FRA > LHR" where the route is known. Airlines increasingly pad their
    // callsigns with a random suffix that a callsign-keyed route database cannot
    // resolve, and for those the operator is the next best thing.
    String RouteLabel() const;

    // "Frankfurt > London" on its own row. Empty when either town is missing.
    String AirportNamesLabel() const;
};

namespace AircraftText
{
    // The display font holds ASCII glyphs only, so the accents that come with
    // airline names from a worldwide database would end up as garbage.
    String AsciiOnly(const String &value);

    // Cut to the last word that still fits, because a name running into the
    // round frame reads worse than a shortened one.
    String Fit(const String &value, unsigned int width);
}

namespace JsonParser
{

    // Expects the value of the "response" key, not the whole document.
    template <>
    AircraftInfo Parse<AircraftInfo>(const JsonVariant &response);

}
