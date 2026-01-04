#include <PixelFlow.h>

using pixelflow::PixelFlow;

// ==========================
// Hardware configuration
// ==========================
#define LED_PIN   5
#define LED_COUNT 4

// ==========================
// Logical LED assignments
// ==========================
#define LED_STATUS  0
#define LED_WIFI    1
#define LED_ERROR   2
#define LED_POWER   3

PixelFlow leds(LED_PIN, LED_COUNT);

void setup()
{
    Serial.begin(115200);

    leds.begin();
    leds.startTask();

    // ----------------------------------
    // Static power LED
    // ----------------------------------
    leds.setPixel(LED_POWER,
        "{ \"mode\":\"on\", \"color\":\"#00FF00\", \"intensity\":80 }");

    // ----------------------------------
    // Status LED pulses blue
    // ----------------------------------
    leds.setPixel(LED_STATUS,
        "{ \"mode\":\"animate\", \"type\":\"pulse\", \"color\":\"#0000FF\", \"interval\":40 }");

    // ----------------------------------
    // WiFi LED blinks fast
    // ----------------------------------
    leds.setPixel(LED_WIFI,
        "{ \"mode\":\"animate\", \"type\":\"blink\", \"interval\":150 }");

    // ----------------------------------
    // Error LED is off initially
    // ----------------------------------
    leds.setPixel(LED_ERROR,
        "{ \"mode\":\"off\" }");

    // ----------------------------------
    // Background ambient ramp on all other LEDs
    // ----------------------------------
    leds.setPixel(PixelFlow::PIXEL_ALL,
        "{ \"mode\":\"ramp\", \"width\":5, \"color\":\"#202020\", \"interval\":40 }");
}

void loop()
{
    static uint32_t lastToggle = 0;
    static bool errorActive = false;

    // Every 5 seconds simulate an error state
    if (millis() - lastToggle > 5000)
    {
        lastToggle = millis();
        errorActive = !errorActive;

        if (errorActive)
        {
            // Turn error LED red and blinking
            leds.setPixel(LED_ERROR,
                "{ \"mode\":\"animate\", \"type\":\"blink\", \"color\":\"#FF0000\", \"interval\":100 }");

            // Dim background LEDs while error is active
            leds.setPixel(PixelFlow::PIXEL_ALL,
                "{ \"mode\":\"intensity\", \"intensity\":30 }");
        }
        else
        {
            // Turn error LED off
            leds.setPixel(LED_ERROR,
                "{ \"mode\":\"off\" }");

            // Restore ramp background
            leds.setPixel(PixelFlow::PIXEL_ALL,
                "{ \"mode\":\"ramp\", \"width\":5, \"color\":\"#202020\", \"interval\":40 }");
        }
    }
}
