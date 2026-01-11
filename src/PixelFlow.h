#pragma once

/************************************************************
    PixelFlow Animation Engine (Parallel, Non-Blocking)
    -------------------------------------------------------

    Deze klasse biedt een krachtige animatie-engine voor
    NeoPixel-LEDs (WS2812/WS2812B) op ESP32, gebaseerd op:

        - Adafruit_NeoPixel (hardware driver)
        - FreeRTOS (non-blocking update loop)
        - ArduinoJson (flexibele commands)
        - Per-pixel state machine (parallel animaties)

    ========================================================
    OVERZICHT
    ========================================================

    Iedere pixel heeft zijn eigen "PixelState":
        • kleur (r,g,b)
        • intensiteit (brightness)
        • mode (off/on/intensity/animate)
        • animType (blink/pulse/random_color)
        • anim parameters (interval, timing, phase, duration, end)
        • alles blijft behouden tot setPixel() deze pixel verandert

    Elke pixel draait zijn eigen animatie PARALLEL:
        • pixel 0 kan pulsen
        • pixel 1–3 kunnen blinken
        • pixel 10 kan ramp/chase doen
        • pixel 5–12 kunnen randomColor doen
        • enz…

    SetPixel() wijzigt ALLEEN de pixels die je selecteert.
    Geen animatie wordt gestopt voor andere pixels.

    Alles is non-blocking:
        • Geen delays in de library
        • update() draait in FreeRTOS task
        • Alle animaties zijn tijdgestuurd

    ========================================================
    JSON COMMANDOS
    ========================================================

    (Nieuwe stijl:

        leds.setPixel(4, R"({
            "mode":"animate",
            "type":"pulse",
            "color":"#00FF00",
            "interval":300,
            "duration":2000,
            "end":"keep"
        })");

    Meerdere pixels:

        leds.setPixel({LED_STATUS, 4, 8}, R"({
            "mode":"animate",
            "type":"blink",
            "interval":150
        })");

    Alles:

        leds.setPixel(PixelFlow::PIXEL_ALL, R"({"mode":"off"})");

    Of meerdere JSON fragments die automatisch gemerged worden:

        leds.setPixel({1,3,5}, {
            R"({"mode":"animate"})",
            R"({"type":"pulse"})",
            R"({"color":"#00FF00"})",
            R"({"interval":300})",
            R"({"duration":2000})",
            R"({"end":"keep"})"
        });

    ========================================================
    PIXEL SELECTIE
    ========================================================

    • Eén pixel:
        leds.setPixel(5, json);

    • Lijst:
        leds.setPixel({1,3,5,7}, json);

    • Alle pixels:
        leds.setPixel(PixelFlow::PIXEL_ALL, json);

    ========================================================
    MODES
    ========================================================

    "mode": "off"        → pixel uit (geen duration)
    "mode": "on"         → pixel aan op vaste brightness (geen duration)
    "mode": "intensity"  → helderheid instellen, zonder animatie (geen duration)
    "mode": "animate"    → pulse/blink/random met duration
    "mode": "ramp"       → chase / comet effect (met width, duration)

    ========================================================
    ANIMATIE TYPES (bij mode "animate")
    ========================================================

        "type": "blink"        → aan/uit knipperen
        "type": "pulse"        → sinus fade in/out
        "type": "random"       → random kleur op interval

    ========================================================
    RAMP / CHASE MODE (mode "ramp")
    ========================================================

    Dit is het “lopende licht + sinus-fade + comet” effect:

        "mode": "ramp",
        "type": "up" | "down",
        "width": 3,
        "color": "random" | "#RRGGBB",
        "interval": 30,
        "intensity": 200,
        "duration": 10000,
        "end": "keep" | "on" | "off"

    Gedrag:

        HEAD pixel        → sinus fade-in naar max
        MIDDEN pixels     → maxBrightness
        TAIL pixel        → sinus fade-out naar 0
        Andere pixels     → onveranderd (tenzij deel van ramp)

        "duration": 0     → onbeperkt tot nieuwe call
        "duration": >0    → stopt na X ms, dan end-behavior

    UP:   eerste pixel -> laatste pixel, bounce
    DOWN: laatste pixel -> eerste pixel, bounce

    ========================================================
    DURATION & END-BEHAVIOR
    ========================================================

    Voor ALLE animaties (mode = animate of ramp):

        "duration": 0       → animatie loopt door tot nieuwe call
        "duration": 2000    → animatie stopt na 2 seconden

    Bij stoppen:

        "end":"keep"   → pixel(s) blijven in laatste visuele state
        "end":"off"    → pixel(s) gaan uit
        "end":"on"     → pixel(s) gaan aan op maxIntensity

    Duration werkt voor ALLE pixels die in de betreffende call
    geselecteerd werden. Andere pixels blijven hun eigen animatie doen.

    ========================================================
    RTOS / MULTITHREADING
    ========================================================

    • Je roept eenmalig aan:
            leds.begin();
            leds.startTask();

    • De library creëert een FreeRTOS task:
            void PixelFlow::taskEntry()

    • update() draait ~100x per seconde

    • Alle setPixel()-calls zijn mutex-beveiligd
      → je mag uit elke task setPixel() aanroepen

************************************************************/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

#include <vector>
#include <string>
#include <initializer_list>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define NEOPIXEL_ALL        -1

namespace pixelFlow {

// RAII mutex
class MutexLock
{
public:
    MutexLock(SemaphoreHandle_t mtx)
        : mutex(mtx), locked(false)
    {
        if (mutex != nullptr)
        {
            if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE)
            {
                locked = true;
            }
        }
    }

    ~MutexLock()
    {
        if (locked && mutex != nullptr)
        {
            xSemaphoreGive(mutex);
        }
    }

private:
    SemaphoreHandle_t mutex;
    bool locked;
};

enum class PixelMode : uint8_t
{
    OFF,
    ON,
    INTENSITY,
    ANIMATE,
    RAMP
};

enum class PixelAnimType : uint8_t
{
    NONE,
    BLINK,
    PULSE,
    RANDOM_COLOR
};

enum class RampDirection : uint8_t
{
    UP,
    DOWN
};

enum class EndBehavior : uint8_t
{
    KEEP,
    OFF,
    ON,
    USE_DEFAULT
};

//-- Struct to hold pending commands for blocked pixels
struct PendingCommand
{
  bool active = false;
  
  PixelMode mode = PixelMode::OFF;
  PixelAnimType animType = PixelAnimType::NONE;
  
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  
  uint8_t baseBrightness = 255;
  uint16_t intervalMs = 500;
  uint32_t animDurationMs = 0;
  EndBehavior endBehavior = EndBehavior::KEEP;
};

struct PixelState
{
    PixelMode mode = PixelMode::OFF;
    PixelAnimType animType = PixelAnimType::NONE;

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    uint8_t baseBrightness = 255;
    uint8_t currentBrightness = 0;

    uint16_t intervalMs = 500;
    uint32_t lastUpdate = 0;

    // Animatie timers
    uint32_t animStartTime = 0;
    uint32_t animDurationMs = 0;     // 0 = oneindig
    EndBehavior endBehavior = EndBehavior::KEEP;

    // Flags voor blink/pulse/random
    bool toggleState = false;
    float phase = 0.0f;
    
    //-- Pending command (for duration-based blocking)
    PendingCommand pending;
};

// Groepsanimatie voor RAMP (chase-effect)
struct RampState
{
    bool active = false;

    std::vector<int> indices;        // pixels in de animatie
    RampDirection direction = RampDirection::UP;

    uint8_t width = 3;
    uint8_t maxIntensity = 200;
    uint16_t intervalMs = 30;

    uint32_t startTime = 0;
    uint32_t durationMs = 0;         // 0 = oneindig
    uint32_t lastStepTime = 0;

    EndBehavior endBehavior = EndBehavior::KEEP;

    bool randomPerPixel = true;
    uint8_t fixedR = 255;
    uint8_t fixedG = 255;
    uint8_t fixedB = 255;

    int headIndex = 0;
    int stepDirection = 1;           // +1 of -1
};

class PixelFlow
{
public:
    // Speciale selector voor "alle pixels"
    static constexpr int PIXEL_ALL = -1;

    PixelFlow(uint8_t pin, uint16_t count);

    void begin();
    void startTask(const char *name = "PixelFlowTask",
                   uint16_t stack = 4096,
                   uint8_t prio = 1,
                   uint8_t core = 1);

    void update();

    // =====================================================
    // Nieuwe API: pixel selectie buiten JSON
    // =====================================================

    // Eén pixel of PIXEL_ALL
    void setPixel(int pixel, const std::string &json);

    // Meerdere pixels
    void setPixel(std::initializer_list<int> pixels, const std::string &json);

    // Variant met JSON fragments (automatisch gemerged)
    void setPixel(int pixel, std::initializer_list<const char *> fragments);
    void setPixel(std::initializer_list<int> pixels, std::initializer_list<const char *> fragments);

    // =====================================================
    // Default JSON per pixel
    // =====================================================

    //-- Sla default JSON op voor een specifieke pixel
    void setDefaultPixel(int pixel, const std::string &json);

    //-- Pas de opgeslagen default configuratie toe op een pixel (als deze bestaat)
    void applyDefaultPixel(int pixel);

private:
    uint8_t pin;
    uint16_t numPixels;
    Adafruit_NeoPixel strip;
    std::vector<PixelState> pixels;
    
    std::vector<std::string> defaults;
    std::vector<bool> hasDefault;

    SemaphoreHandle_t mutexHandle;
    TaskHandle_t taskHandle;

    RampState ramp;

    static void taskEntry(void *param);

    void applyStateToStrip();

    // =====================================================
    // Helpers: target selectie
    // =====================================================
    std::vector<int> makeTargets(int pixel) const;
    std::vector<int> makeTargets(std::initializer_list<int> list) const;

    // =====================================================
    // Helpers: JSON merge + parsing
    // =====================================================
    bool mergeFragmentsIntoDoc(JsonDocument &doc, std::initializer_list<const char *> fragments);

    // Pas instellingen toe op targets
    void applyJsonToTargets(JsonDocument &doc, const std::vector<int> &targets);

    // Parsers
    void parseColor(JsonVariantConst col, PixelState &p);
    PixelMode toMode(const char *s);
    PixelAnimType toAnim(const char *s);
    RampDirection toRampDirection(const char *s);
    EndBehavior toEndBehavior(const char *s);

    void configureRampFromJson(JsonDocument &doc, const std::vector<int> &targets);
    void stepRamp(uint32_t now);
    
    //-- Check if pixel is blocked by active duration-based animation
    bool isPixelBlocked(int idx, uint32_t now) const;
};

} // namespace pixelFlow
