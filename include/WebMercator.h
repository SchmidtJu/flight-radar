#pragma once

#include <cmath>

// Web Mercator (EPSG:3857), the projection OpenStreetMap tiles are cut in.
// Pure functions, no state - the caller owns centre, zoom and viewport.
namespace WebMercator
{
    constexpr int TileSize = 256;
    constexpr int ViewportSize = 240; // GC9A01 panel, square

    constexpr int MinZoom = 2;
    constexpr int MaxZoom = 16;

    // Mercator diverges towards the poles; tile services cut it off here so that
    // the world comes out square.
    constexpr double MaxLatitude = 85.05112878;

    constexpr double DegToRad = M_PI / 180.0;

    // 2^zoom = ZoomNumerator * cos(lat) / radiusDeg, see SelectZoom
    constexpr double ZoomNumerator = (static_cast<double>(ViewportSize) * 360.0) / (2.0 * TileSize);

    inline double ClampLatitude(double lat)
    {
        if (lat > MaxLatitude)
            return MaxLatitude;
        if (lat < -MaxLatitude)
            return -MaxLatitude;

        return lat;
    }

    inline double WorldSize(int zoom)
    {
        return static_cast<double>(TileSize) * std::pow(2.0, zoom);
    }

    inline double LonToWorldPx(double lon, int zoom)
    {
        return (lon + 180.0) / 360.0 * WorldSize(zoom);
    }

    inline double LatToWorldPx(double lat, int zoom)
    {
        const double latRad = ClampLatitude(lat) * DegToRad;
        const double y = std::log(std::tan(latRad) + 1.0 / std::cos(latRad));

        return (1.0 - y / M_PI) / 2.0 * WorldSize(zoom);
    }

    // Inverses of the two above. Needed to turn the viewport edges back into a
    // geographic box, for instance for the OpenSky bounding box.
    inline double WorldPxToLon(double px, int zoom)
    {
        return px / WorldSize(zoom) * 360.0 - 180.0;
    }

    inline double WorldPxToLat(double py, int zoom)
    {
        const double y = (1.0 - 2.0 * py / WorldSize(zoom)) * M_PI;

        return std::atan(std::sinh(y)) / DegToRad;
    }

    // Picks the zoom whose native tile resolution comes closest to showing
    // 2 * radiusDeg degrees of latitude across the viewport. Snapping to a whole
    // zoom level is what lets the tiles be blitted 1:1 without resampling.
    //
    // From ViewportSize = 2 * radiusDeg * WorldSize(z) / (360 * cos(lat)) follows
    // 2^z = ZoomNumerator * cos(lat) / radiusDeg.
    inline int SelectZoom(double lat, double radiusDeg)
    {
        if (radiusDeg <= 0.0)
            return MaxZoom;

        const double scale = ZoomNumerator * std::cos(ClampLatitude(lat) * DegToRad) / radiusDeg;
        if (scale <= 0.0)
            return MinZoom;

        const long zoom = std::lround(std::log2(scale));

        if (zoom < MinZoom)
            return MinZoom;
        if (zoom > MaxZoom)
            return MaxZoom;

        return static_cast<int>(zoom);
    }

    // The radius SelectZoom actually snapped to. Callers need this rather than the
    // configured radius, otherwise map and aircraft drift apart.
    inline double EffectiveRadius(double lat, int zoom)
    {
        return ZoomNumerator * std::cos(ClampLatitude(lat) * DegToRad) / std::pow(2.0, zoom);
    }
}
