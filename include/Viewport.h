#pragma once

#include <cmath>

#include "WebMercator.h"

// An unconfigured radius reads back as 0, which would send SelectZoom to maximum
// zoom and show a few hundred metres.
constexpr double DefaultRadiusDeg = 0.2;

// The single description of what the panel shows: zoom level, the world pixel in
// the top left corner, and the geographic box that spans. Map tiles and aircraft
// positions are both derived from this, which is what keeps them aligned.
struct Viewport
{
    int zoom = 0;

    double originX = 0.0;
    double originY = 0.0;

    // The radius the zoom level snapped to, in degrees of latitude.
    double effectiveRadius = 0.0;

    [[nodiscard]] double LonMin() const
    {
        return WebMercator::WorldPxToLon(originX, zoom);
    }

    [[nodiscard]] double LonMax() const
    {
        return WebMercator::WorldPxToLon(originX + WebMercator::ViewportSize, zoom);
    }

    // Screen y grows southwards, so the top edge is the larger latitude.
    [[nodiscard]] double LatMax() const
    {
        return WebMercator::WorldPxToLat(originY, zoom);
    }

    [[nodiscard]] double LatMin() const
    {
        return WebMercator::WorldPxToLat(originY + WebMercator::ViewportSize, zoom);
    }
};

inline Viewport MakeViewport(double lat, double lon, double radiusDeg)
{
    const double radius = radiusDeg > 0.0 ? radiusDeg : DefaultRadiusDeg;

    Viewport viewport;

    viewport.zoom = WebMercator::SelectZoom(lat, radius);
    viewport.effectiveRadius = WebMercator::EffectiveRadius(lat, viewport.zoom);

    // Whole pixels only: tiles can only be blitted at integer positions, so the
    // aircraft projection has to work off the same rounded origin. A fractional
    // origin would offset the two against each other by up to a pixel.
    viewport.originX = std::floor(WebMercator::LonToWorldPx(lon, viewport.zoom) - WebMercator::ViewportSize / 2.0);
    viewport.originY = std::floor(WebMercator::LatToWorldPx(lat, viewport.zoom) - WebMercator::ViewportSize / 2.0);

    return viewport;
}
