#pragma once

#include <cstdint>

// Recolours standard OpenStreetMap Carto tiles into a dark radar theme.
//
// Inverting the tiles wholesale does not work: forest turns magenta, water
// turns brown, and white minor roads end up darker than the land around them.
// Carto does use a small and fixed set of fill colours though, so a
// nearest-colour mapping onto hand-picked dark equivalents keeps the meaning
// intact - water still reads as water, and roads stay the brightest lines.
namespace MapDarkTheme
{
    struct PaletteEntry
    {
        uint32_t source; // as served by tile.openstreetmap.org
        uint32_t target; // dark theme replacement
    };

    constexpr PaletteEntry Palette[] = {
        {0xF2EFE9, 0x0D1117}, // land background
        {0xE0DFDF, 0x161B22}, // residential area
        {0xD9D0C9, 0x21262D}, // building
        {0xAAD3DF, 0x10202E}, // water
        // The greens are deliberately closer to neutral than a faithful dark
        // recolour would be. OSM land use is heavily speckled, and the aircraft
        // symbols and the radar sweep are green too - a saturated green map
        // fights both.
        {0xADD19E, 0x101A13}, // forest
        {0xCDEBB0, 0x13211A}, // park, grass
        {0xEEF0D5, 0x14170F}, // farmland
        {0xEBDBE8, 0x1C1A22}, // industrial
        {0xE892A2, 0x3A3F4A}, // motorway
        {0xF9B29C, 0x3A3F4A}, // trunk road
        {0xFCD6A4, 0x2E333D}, // primary road
        {0xF7FABF, 0x2A2F38}, // secondary road
        {0xFFFFFF, 0x262B33}, // minor road fill
        {0xCCCCCC, 0x1E2229}, // road casing, fences, railways
        {0x333333, 0x5A6472}, // label text
    };

    // The two ends of the fallback ramp: the land background and the brightest
    // ink the theme allows itself, which is a touch above the label colour.
    constexpr uint32_t Background = 0x0D1117;
    constexpr uint32_t Ink = 0x6B7684;

    // Beyond this distance a pixel is not one of the fills above but something
    // the renderer composed - antialiasing around labels, POI icons - and the
    // luminance ramp takes over. 55 units per channel sits well above the gap
    // between neighbouring Carto fills, so real fills still snap.
    constexpr uint32_t MatchThreshold = 9 * 55 * 55;

    // Perceptual weights in the spirit of Rec. 601. Unweighted euclidean
    // distance mixes up the pink motorway with the beige primary road.
    inline uint32_t WeightedDistance(uint8_t r, uint8_t g, uint8_t b, uint32_t rgb)
    {
        const int dr = static_cast<int>(r) - static_cast<int>((rgb >> 16) & 0xFF);
        const int dg = static_cast<int>(g) - static_cast<int>((rgb >> 8) & 0xFF);
        const int db = static_cast<int>(b) - static_cast<int>(rgb & 0xFF);

        return static_cast<uint32_t>(2 * dr * dr + 4 * dg * dg + 3 * db * db);
    }

    // t runs 0..255 and picks how much of "to" ends up in the result.
    inline uint32_t Blend(uint32_t from, uint32_t to, uint32_t t)
    {
        const uint32_t r = (((from >> 16) & 0xFF) * (255 - t) + ((to >> 16) & 0xFF) * t) / 255;
        const uint32_t g = (((from >> 8) & 0xFF) * (255 - t) + ((to >> 8) & 0xFF) * t) / 255;
        const uint32_t b = ((from & 0xFF) * (255 - t) + (to & 0xFF) * t) / 255;

        return (r << 16) | (g << 8) | b;
    }

    inline uint32_t Dim(uint32_t rgb, int brightness)
    {
        if (brightness >= 100)
            return rgb;

        const uint32_t r = (((rgb >> 16) & 0xFF) * brightness) / 100;
        const uint32_t g = (((rgb >> 8) & 0xFF) * brightness) / 100;
        const uint32_t b = ((rgb & 0xFF) * brightness) / 100;

        return (r << 16) | (g << 8) | b;
    }

    // Dimming is deliberately not part of this: the panel can switch the
    // recolouring off and still want the map dimmed, so the caller applies the
    // two stages separately.
    inline uint32_t Recolour(uint8_t r, uint8_t g, uint8_t b)
    {
        // Pure black means "no tile here": the provider clears the sprite to
        // black, and a download that failed leaves that showing. Sending it
        // through the ramp below would turn the gap into light grey.
        if ((r | g | b) == 0)
            return 0;

        uint32_t closest = MatchThreshold;
        uint32_t result = 0;
        bool matched = false;

        for (const PaletteEntry &entry : Palette)
        {
            const uint32_t distance = WeightedDistance(r, g, b, entry.source);

            if (distance < closest)
            {
                closest = distance;
                result = entry.target;
                matched = true;
            }
        }

        if (!matched)
        {
            // OSM draws dark ink on light paper and the dark theme needs the
            // reverse, so the ramp is inverted: near-white becomes background,
            // near-black becomes ink.
            const uint32_t luminance = (77u * r + 150u * g + 29u * b) >> 8;

            result = Blend(Background, Ink, 255 - luminance);
        }

        return result;
    }
}
