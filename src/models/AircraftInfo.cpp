#include "models/AircraftInfo.h"

namespace
{
    // Where the route row sits inside the circle it holds about this many
    // characters of the display font.
    constexpr unsigned int RowWidth = 33;

    // Airports without an IATA code exist, and the route line only has room for
    // a code, so the ICAO one stands in. Missing keys resolve to null all the
    // way down, which is why the lookup needs no null checks of its own.
    template <typename TRoute>
    String ReadAirportCode(TRoute route, const char *airportKey)
    {
        if (!route[airportKey]["iata_code"].isNull())
            return route[airportKey]["iata_code"].template as<String>();

        if (!route[airportKey]["icao_code"].isNull())
            return route[airportKey]["icao_code"].template as<String>();

        return "";
    }

    // The town, not the airport name: "Frankfurt am Main" beats "Frankfurt am
    // Main Airport" on a row this narrow, and it is what anyone would say out
    // loud anyway.
    template <typename TRoute>
    String ReadAirportCity(TRoute route, const char *airportKey)
    {
        if (route[airportKey]["municipality"].isNull())
            return "";

        return AircraftText::AsciiOnly(
            route[airportKey]["municipality"].template as<String>());
    }
}

String AircraftInfo::RouteLabel() const
{
    if (HasRoute())
        return originIata + " > " + destinationIata;

    // A handful of operator names run past the row.
    return AircraftText::Fit(OperatorLabel(), RowWidth - 5);
}

String AircraftInfo::AirportNamesLabel() const
{
    if (!HasAirportNames())
        return "";

    const String both = originCity + " > " + destinationCity;

    if (both.length() <= RowWidth)
        return both;

    return AircraftText::Fit(both, RowWidth);
}

namespace AircraftText
{
    namespace
    {
        // U+00C0 to U+00FF folded onto the closest ASCII letter, indexed by the
        // trailing byte of their UTF-8 pair. Sixty-four bytes of flash buys
        // "Zurich" and "Munchen" instead of "Zrich" and "Mnchen".
        const char Latin1Folded[] =
            "AAAAAAACEEEEIIII"  // C0-CF
            "DNOOOOOxOUUUUYPs"  // D0-DF
            "aaaaaaaceeeeiiii"  // E0-EF
            "dnooooo/ouuuuypy"; // F0-FF
    }

    String AsciiOnly(const String &value)
    {
        String result;
        result.reserve(value.length());

        for (unsigned int i = 0; i < value.length(); i++)
        {
            const uint8_t c = (uint8_t)value[i];

            if (c >= 0x20 && c <= 0x7E)
            {
                result += (char)c;
                continue;
            }

            const bool hasTrail = i + 1 < value.length();
            const uint8_t trail = hasTrail ? (uint8_t)value[i + 1] : 0;

            if (c == 0xC3 && trail >= 0x80 && trail <= 0xBF)
            {
                result += Latin1Folded[trail - 0x80];
                i++;
                continue;
            }

            // Beyond Latin-1 there is no letter worth guessing at, so the
            // character's continuation bytes leave with it rather than becoming
            // separate garbage.
            while (c >= 0xC0 && i + 1 < value.length() && ((uint8_t)value[i + 1] & 0xC0) == 0x80)
                i++;
        }

        result.trim();

        return result;
    }

    String Fit(const String &value, unsigned int width)
    {
        if (value.length() <= width)
            return value;

        const int lastSpace = value.substring(0, width).lastIndexOf(' ');

        return value.substring(0, lastSpace > 0 ? lastSpace : width);
    }

}

namespace JsonParser
{

    template <>
    AircraftInfo Parse<AircraftInfo>(const JsonVariant &response)
    {
        AircraftInfo info;

        if (!response["aircraft"]["manufacturer"].isNull())
            info.manufacturer = response["aircraft"]["manufacturer"].as<String>();

        if (!response["aircraft"]["icao_type"].isNull())
            info.icaoType = response["aircraft"]["icao_type"].as<String>();

        if (!response["aircraft"]["registration"].isNull())
            info.registration = AircraftText::AsciiOnly(
                response["aircraft"]["registration"].as<String>());

        if (!response["aircraft"]["registered_owner"].isNull())
            info.owner = AircraftText::AsciiOnly(
                response["aircraft"]["registered_owner"].as<String>());

        info.originIata = ReadAirportCode(response["flightroute"], "origin");
        info.destinationIata = ReadAirportCode(response["flightroute"], "destination");
        info.originCity = ReadAirportCity(response["flightroute"], "origin");
        info.destinationCity = ReadAirportCity(response["flightroute"], "destination");

        // Free of charge whenever a route came back. Callsigns without one need
        // a second request, which is the provider's job.
        if (!response["flightroute"]["airline"]["name"].isNull())
            info.airline = AircraftText::AsciiOnly(
                response["flightroute"]["airline"]["name"].as<String>());

        return info;
    }

}
