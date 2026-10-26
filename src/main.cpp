#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
#include "MapTileProvider.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"
#include <ESP32Encoder.h>
#include <Adafruit_NeoPixel.h>

#define ENCODER_A 9
#define ENCODER_B 8
#define ENCODER_SW 7

#ifndef RGB_LED_PIN
#define RGB_LED_PIN 21
#endif

Adafruit_NeoPixel statusLed(
    1,
    RGB_LED_PIN,
    NEO_GRB + NEO_KHZ800);

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);
constexpr int DEFAULT_BACKLIGHT_PERCENT = 100;

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

ESP32Encoder encoder;
int64_t lastEncoderPos = 0;

AircraftManager aircraftManager(configServer, authHandler, http, tft);
MapTileProvider mapProvider(configServer, http, tft);

void SetLed(uint8_t r, uint8_t g, uint8_t b)
{
  statusLed.setPixelColor(
      0,
      statusLed.Color(r, g, b));

  statusLed.show();
}

uint8_t BacklightFromConfig()
{
  const String stored = configServer.GetStoredString("backlight");

  // Unconfigured means full brightness. The lower bound stops a stray 0 from
  // making the device look dead with no way back except the serial log.
  const int percent = stored.isEmpty() ? DEFAULT_BACKLIGHT_PERCENT : constrain(stored.toInt(), 5, 100);

  return static_cast<uint8_t>((percent * 255) / 100);
}

void PrintBootDiagnostics()
{
  Serial.printf("Chip: %s rev %d, %d core(s)\n",
                ESP.getChipModel(),
                ESP.getChipRevision(),
                ESP.getChipCores());

  Serial.printf("Flash: %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("PSRAM: %u bytes (%u free)\n", ESP.getPsramSize(), ESP.getFreePsram());
  Serial.printf("Heap: %u bytes free\n", ESP.getFreeHeap());

  if (ESP.getPsramSize() == 0)
  {
    Serial.println("WARNING: no PSRAM detected - check board_build.psram_type / memory_type in platformio.ini");
  }
}

void setup()
{
  Serial.begin(115200);

  // USB CDC swallows anything printed before the host attaches, and the boot
  // diagnostics are the only signal that PSRAM came up. Serial turns true as
  // soon as the USB bus is enumerated, but a reset re-enumerates the device and
  // the host needs another moment to reopen the port - hence the grace period.
  // On a plain USB power supply there is no data host, so neither wait applies.
  while (!Serial && millis() < 3000)
  {
    delay(10);
  }

  if (Serial)
  {
    delay(1500);
  }

  PrintBootDiagnostics();

  statusLed.begin();
  statusLed.setBrightness(200);
  statusLed.clear();
  statusLed.show();

  SetLed(0, 0, 255); // Booting

  // initialise LGFX + screen
  tft.init();
  tft.invertDisplay(true);

  // The panel can be mounted either way up: rotation 2 is the default, 0 is the
  // same picture turned by 180 degrees. LovyanGFX puts this into the GC9A01's
  // MADCTL register, so it applies to everything drawn afterwards - boot
  // screen, backbuffer and map alike.
  tft.setRotation(configServer.GetStoredString("flip") == "true" ? 0 : 2);

  tft.setBrightness(BacklightFromConfig());

  // 16 bpp instead of 8: the RGB332 palette would break the map into bands.
  backbuffer.setColorDepth(16);

  if (backbuffer.createSprite(SCREEN_SIZE, SCREEN_SIZE) == nullptr)
  {
    // Internal RAM is faster and DMA-capable, so it is the first choice for a
    // buffer that gets rewritten every frame. PSRAM is the fallback.
    Serial.println("[BOOT] backbuffer did not fit into internal RAM, moving it to PSRAM");

    backbuffer.setPsram(true);
    backbuffer.createSprite(SCREEN_SIZE, SCREEN_SIZE);
  }

  // establish WiFi connection
  tft.fillScreen(lgfx::color888(0, 0, 0));
  tft.setTextColor(lgfx::color888(0, 255, 0));
  tft.drawCentreString("Connecting to WiFi...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);

  SetLed(255, 255, 0); // WiFi connecting

  WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);
  wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

  SetLed(0, 255, 0); // Running

  // Wall-clock time, needed so the map cache can expire. SNTP answers in the
  // background, so this does not hold up the boot; a cache written before the
  // first answer simply carries no timestamp.
  configTime(0, 0, "pool.ntp.org");

  // begin background server for configuration
  configServer.Initialise();

  // fetch the map background - up to four tile downloads, so say so on screen
  tft.fillScreen(lgfx::color888(0, 0, 0));
  tft.drawCentreString("Loading map...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);
  tft.drawCentreString("(c) OpenStreetMap contributors", SCREEN_SIZE / 2, SCREEN_SIZE / 2 + 20);

  mapProvider.Initialise();

  // initialise aircraft manager
  aircraftManager.Initialise();

  ESP32Encoder::useInternalWeakPullResistors = puType::up;

  encoder.attachSingleEdge(ENCODER_A, ENCODER_B);
  encoder.setCount(0);

  pinMode(ENCODER_SW, INPUT_PULLUP);
}

void loop()
{
  // Done here rather than in the request handler: refetching blocks for several
  // seconds, which the async web server task must not do.
  if (configServer.ConsumeMapReloadRequest())
  {
    tft.fillScreen(lgfx::color888(0, 0, 0));
    tft.drawCentreString("Reloading map...", SCREEN_SIZE / 2, SCREEN_SIZE / 2);

    // Bypassing the cache is the entire point of the button.
    mapProvider.Initialise(true);
  }

  mapProvider.UpdateCacheTimestampWhenClockArrives();

  int64_t pos = encoder.getCount();

  if (pos != lastEncoderPos)
  {
    if (pos > lastEncoderPos)
    {
      aircraftManager.SelectNextAircraft();
    }
    else
    {
      aircraftManager.SelectPreviousAircraft();
    }

    lastEncoderPos = pos;
  }

  static bool lastButtonState = HIGH;

  bool currentButtonState = digitalRead(ENCODER_SW);

  if (lastButtonState == HIGH &&
      currentButtonState == LOW)
  {
    aircraftManager.EncoderClick();
  }

  lastButtonState = currentButtonState;

  SetLed(0, 255, 255); // Fetching
  aircraftManager.Update();

  // draw cycle
  if (mapProvider.IsReady())
  {
    mapProvider.DrawTo(backbuffer);
  }
  else
  {
    backbuffer.fillScreen(lgfx::color888(0, 0, 0));
  }

  String renderScanlines = configServer.GetStoredString("scanline");
  if (renderScanlines.isEmpty() || renderScanlines == "true")
  {
    DrawScanLines(backbuffer,
                  SCREEN_SIZE_DIV_2 - 1,
                  SCREEN_SIZE_DIV_2 - 1,
                  SCREEN_SIZE_DIV_2 - 1 + (std::cos(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
                  SCREEN_SIZE_DIV_2 - 1 + (std::sin(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
                  20, 128, 5);
  }

  aircraftManager.Draw(backbuffer);
  backbuffer.pushSprite(0, 0);

  SetLed(0, 255, 0); // Running
}
