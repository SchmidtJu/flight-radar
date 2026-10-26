#include "MapTileProvider.h"

#include "MapDarkTheme.h"

#include <LittleFS.h>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>

namespace
{
    constexpr char CachePath[] = "/map.raw";

    // Bumped whenever CacheHeader or the pixel format below changes, so an old
    // file is rejected instead of misread.
    constexpr uint32_t CacheMagic = 0x4D524331UL; // "MRC1"

    constexpr uint16_t CacheVersion = 1;

    constexpr size_t CachePixelBytes = static_cast<size_t>(WebMercator::ViewportSize) * WebMercator::ViewportSize * 2;

    // The tile policy asks for at least seven days of local caching. This is
    // therefore the earliest a refetch is allowed, not a deadline.
    constexpr uint32_t CacheTtlSeconds = 7UL * 24UL * 60UL * 60UL;

    // Below this the clock is not real, it is the epoch default because SNTP has
    // not answered yet.
    constexpr uint32_t PlausibleEpoch = 1600000000UL; // September 2020

    // Everything that changes the stored picture. The derived zoom and origin
    // are compared rather than lat/lon/radius: they are integers, so no float
    // tolerance is needed, and two positions that round to the same origin do
    // produce the same image.
    struct CacheHeader
    {
        uint32_t magic;
        uint16_t version;
        uint16_t viewportSize;
        int32_t zoom;
        int32_t originX;
        int32_t originY;
        uint8_t brightness;
        uint8_t darkFilter;
        uint16_t padding;
        uint32_t createdAt; // unix seconds, 0 when the clock was not set yet
    };

    // The palette in MapDarkTheme is already a dark theme, so the default does
    // not dim on top of it. The keys are what the web panel writes.
    constexpr int DefaultBrightness = 100;

    constexpr char BrightnessKey[] = "map-brightness";

    constexpr char DarkFilterKey[] = "map-dark";

    constexpr char MapEnabledKey[] = "map";

    // The panel is round: everything outside this radius is behind the bezel.
    constexpr float ClipRadius = 119.0f;

    // The OSM tile policy rejects library default user agents, so identify the
    // project. Without this the server answers 403.
    constexpr char TileUserAgent[] = "flight-radar/1.0 (+https://github.com/SchmidtJu/flight-radar)";

    constexpr char TileUrlBase[] = "https://tile.openstreetmap.org/";
}

int MapTileProvider::BrightnessFromConfig()
{
    // An unconfigured key reads as an empty string, and toInt() would turn that
    // into 0 - a black map. The lower bound of 5 guards the same mistake made
    // through the panel.
    const String stored = configServer.GetStoredString(BrightnessKey);

    return stored.isEmpty() ? DefaultBrightness : constrain(stored.toInt(), 5, 100);
}

bool MapTileProvider::DarkFilterFromConfig()
{
    return configServer.GetStoredString(DarkFilterKey) != "false";
}

void MapTileProvider::Initialise(bool ignoreCache)
{
    ready = false;

    // Unconfigured means on, so a device that never saw the panel still gets a
    // map. Only an explicit "false" turns it off - and then nothing is
    // downloaded at all, which is both faster and easier on the tile server.
    if (configServer.GetStoredString(MapEnabledKey) == "false")
    {
        Serial.println("[MAP] map background switched off in the configuration");
        return;
    }

    const double lat = configServer.GetStoredString("latitude").toDouble();
    const double lon = configServer.GetStoredString("longitude").toDouble();
    const double radius = configServer.GetStoredString("radius").toDouble();

    viewport = MakeViewport(lat, lon, radius);

    Serial.printf("[MAP] centre %.5f/%.5f, radius %.4f -> zoom %d, effective radius %.4f\n",
                  lat,
                  lon,
                  radius,
                  viewport.zoom,
                  viewport.effectiveRadius);

    // Allocate once: the reload button may call this again, and reallocating
    // would burn 115 KB of PSRAM every time.
    if (mapSprite.getBuffer() == nullptr)
    {
        mapSprite.setColorDepth(16);
        mapSprite.setPsram(true);

        if (mapSprite.createSprite(WebMercator::ViewportSize, WebMercator::ViewportSize) == nullptr)
        {
            Serial.println("[MAP] could not create the map sprite - no map background");
            return;
        }
    }

    // A cache hit skips four TLS downloads and the post-processing, because the
    // stored bitmap is already the finished picture.
    if (!ignoreCache && LoadFromCache())
    {
        ready = true;
        return;
    }

    mapSprite.fillScreen(lgfx::color888(0, 0, 0));

    ready = FetchTiles();

    if (ready)
    {
        PostProcess();
        SaveToCache();
    }
}

bool MapTileProvider::FetchTiles()
{
    const int tileCount = 1 << viewport.zoom;

    const int firstTileX = static_cast<int>(std::floor(viewport.originX / WebMercator::TileSize));
    const int lastTileX = static_cast<int>(std::floor((viewport.originX + WebMercator::ViewportSize - 1) / WebMercator::TileSize));
    const int firstTileY = static_cast<int>(std::floor(viewport.originY / WebMercator::TileSize));
    const int lastTileY = static_cast<int>(std::floor((viewport.originY + WebMercator::ViewportSize - 1) / WebMercator::TileSize));

    int requested = 0;
    int decoded = 0;

    for (int tileY = firstTileY; tileY <= lastTileY; ++tileY)
    {
        // No tiles exist beyond the poles; that part of the viewport stays black.
        if (tileY < 0 || tileY >= tileCount)
            continue;

        for (int tileX = firstTileX; tileX <= lastTileX; ++tileX)
        {
            ++requested;

            // Longitude wraps at the date line, so the tile column does too.
            const int wrappedX = ((tileX % tileCount) + tileCount) % tileCount;

            int destX = static_cast<int>(tileX * WebMercator::TileSize - viewport.originX);
            int destY = static_cast<int>(tileY * WebMercator::TileSize - viewport.originY);

            // A tile starting left of or above the viewport must not be drawn at
            // a negative position; let the decoder skip into the image instead.
            int offX = 0;
            int offY = 0;

            if (destX < 0)
            {
                offX = -destX;
                destX = 0;
            }

            if (destY < 0)
            {
                offY = -destY;
                destY = 0;
            }

            const String url = String(TileUrlBase) + viewport.zoom + "/" + wrappedX + "/" + tileY + ".png";

            uint8_t *data = nullptr;
            size_t length = 0;

            const HttpResult result = http.GetToBuffer(url, &data, &length, TileUserAgent);

            if (!result.success)
            {
                Serial.printf("[MAP] tile %d/%d/%d failed: %s\n", viewport.zoom, wrappedX, tileY, result.errorMessage.c_str());
                continue;
            }

            const bool drawn = mapSprite.drawPng(data, length, destX, destY, 0, 0, offX, offY);

            // Release before the next request: TLS needs about 45 KB of heap, so
            // tiles are fetched one at a time and never held together.
            free(data);

            if (!drawn)
            {
                Serial.printf("[MAP] tile %d/%d/%d could not be decoded (%u bytes)\n", viewport.zoom, wrappedX, tileY, length);
                continue;
            }

            ++decoded;
        }
    }

    Serial.printf("[MAP] %d of %d tiles decoded\n", decoded, requested);

    return decoded > 0;
}

void MapTileProvider::PostProcess()
{
    constexpr int Size = WebMercator::ViewportSize;

    const int brightness = BrightnessFromConfig();

    // Three independent stages, because only the recolouring is optional. The
    // clip has to happen either way or tile corners end up behind the bezel,
    // and dimming an untouched OSM map is a usable look of its own.
    const bool recolour = DarkFilterFromConfig();

    const uint32_t startedAt = millis();

    // Row at a time: 720 bytes on the stack instead of a second full-size
    // buffer, and the typed overloads convert to and from the sprite's RGB565
    // without any assumption about byte order.
    lgfx::bgr888_t row[Size];

    constexpr float Centre = (Size - 1) / 2.0f;
    constexpr float ClipRadiusSq = ClipRadius * ClipRadius;

    for (int y = 0; y < Size; ++y)
    {
        mapSprite.readRectRGB(0, y, Size, 1, row);

        const float dy = y - Centre;

        for (int x = 0; x < Size; ++x)
        {
            const float dx = x - Centre;

            if (dx * dx + dy * dy > ClipRadiusSq)
            {
                row[x] = lgfx::bgr888_t(0, 0, 0);
                continue;
            }

            uint32_t colour = recolour
                                  ? MapDarkTheme::Recolour(row[x].r, row[x].g, row[x].b)
                                  : (static_cast<uint32_t>(row[x].r) << 16) | (static_cast<uint32_t>(row[x].g) << 8) | row[x].b;

            colour = MapDarkTheme::Dim(colour, brightness);

            row[x] = lgfx::bgr888_t((colour >> 16) & 0xFF, (colour >> 8) & 0xFF, colour & 0xFF);
        }

        mapSprite.pushImage(0, y, Size, 1, row);
    }

    Serial.printf("[MAP] post-processed in %lu ms: dark filter %s, brightness %d%%\n",
                  millis() - startedAt,
                  recolour ? "on" : "off",
                  brightness);
}

bool MapTileProvider::LoadFromCache()
{
    // Mounting on demand and formatting on the first run: a fresh device has no
    // filesystem, and failing to mount only costs the cache, not the map.
    if (!LittleFS.begin(true))
    {
        Serial.println("[MAP] LittleFS could not be mounted - running without a cache");
        return false;
    }

    File file = LittleFS.open(CachePath, "r");

    if (!file)
    {
        Serial.println("[MAP] no cached map yet");
        return false;
    }

    CacheHeader header{};

    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) ||
        file.size() != sizeof(header) + CachePixelBytes)
    {
        // Truncated, which is what an interrupted write leaves behind.
        Serial.println("[MAP] cached map is incomplete, refetching");
        file.close();
        return false;
    }

    const bool matches = header.magic == CacheMagic &&
                         header.version == CacheVersion &&
                         header.viewportSize == WebMercator::ViewportSize &&
                         header.zoom == viewport.zoom &&
                         header.originX == static_cast<int32_t>(viewport.originX) &&
                         header.originY == static_cast<int32_t>(viewport.originY) &&
                         header.brightness == BrightnessFromConfig() &&
                         header.darkFilter == (DarkFilterFromConfig() ? 1 : 0);

    if (!matches)
    {
        Serial.println("[MAP] cached map does not match the current settings, refetching");
        file.close();
        return false;
    }

    const uint32_t now = static_cast<uint32_t>(time(nullptr));

    // A file written before SNTP answered carries no timestamp and would never
    // expire, so it gets one as soon as the clock shows up.
    cacheTimestampPending = header.createdAt == 0;

    if (header.createdAt >= PlausibleEpoch && now >= PlausibleEpoch)
    {
        const uint32_t age = now > header.createdAt ? now - header.createdAt : 0;

        if (age > CacheTtlSeconds)
        {
            Serial.printf("[MAP] cached map is %lu days old, refetching\n", age / 86400UL);
            file.close();
            return false;
        }
    }
    else
    {
        // No usable clock on either side, so the age cannot be judged. Keeping
        // the file is the policy-compliant choice - seven days is a minimum, not
        // a deadline - and the reload button is always available.
        Serial.println("[MAP] cached map age unknown, no clock available");
    }

    const uint32_t startedAt = millis();

    const size_t read = file.read(static_cast<uint8_t *>(mapSprite.getBuffer()), CachePixelBytes);

    file.close();

    if (read != CachePixelBytes)
    {
        Serial.printf("[MAP] cached map could only be read to %u of %u bytes\n", read, CachePixelBytes);
        return false;
    }

    Serial.printf("[MAP] loaded from cache in %lu ms\n", millis() - startedAt);

    return true;
}

void MapTileProvider::SaveToCache()
{
    if (!LittleFS.begin(true))
    {
        return;
    }

    File file = LittleFS.open(CachePath, "w");

    if (!file)
    {
        Serial.println("[MAP] could not open the cache for writing");
        return;
    }

    const uint32_t now = static_cast<uint32_t>(time(nullptr));

    CacheHeader header{};
    header.magic = CacheMagic;
    header.version = CacheVersion;
    header.viewportSize = WebMercator::ViewportSize;
    header.zoom = viewport.zoom;
    header.originX = static_cast<int32_t>(viewport.originX);
    header.originY = static_cast<int32_t>(viewport.originY);
    header.brightness = static_cast<uint8_t>(BrightnessFromConfig());
    header.darkFilter = DarkFilterFromConfig() ? 1 : 0;
    header.createdAt = now >= PlausibleEpoch ? now : 0;

    const uint32_t startedAt = millis();

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) +
                           file.write(static_cast<const uint8_t *>(mapSprite.getBuffer()), CachePixelBytes);

    file.close();

    if (written != sizeof(header) + CachePixelBytes)
    {
        // A partial file would be rejected on the next boot anyway, but leaving
        // it around wastes space in a 896 KB partition.
        Serial.printf("[MAP] cache write fell short at %u bytes, removing the file\n", written);
        LittleFS.remove(CachePath);
        return;
    }

    cacheTimestampPending = header.createdAt == 0;

    Serial.printf("[MAP] cached %u bytes in %lu ms%s\n",
                  written,
                  millis() - startedAt,
                  header.createdAt == 0 ? " (without a timestamp, clock not set)" : "");
}

void MapTileProvider::UpdateCacheTimestampWhenClockArrives()
{
    if (!cacheTimestampPending)
    {
        return;
    }

    const uint32_t now = static_cast<uint32_t>(time(nullptr));

    if (now < PlausibleEpoch)
    {
        return;
    }

    // One attempt either way: a second failure would repeat every frame.
    cacheTimestampPending = false;

    File file = LittleFS.open(CachePath, "r+");

    if (!file)
    {
        return;
    }

    file.seek(offsetof(CacheHeader, createdAt));

    const size_t written = file.write(reinterpret_cast<const uint8_t *>(&now), sizeof(now));

    file.close();

    Serial.printf("[MAP] cache timestamp written after %lu ms of uptime%s\n",
                  millis(),
                  written == sizeof(now) ? "" : " - failed");
}

void MapTileProvider::DrawTo(LGFX_Sprite &dst)
{
    if (!ready)
        return;

    mapSprite.pushSprite(&dst, 0, 0);
}
