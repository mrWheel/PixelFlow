# PixelFlow
Realtime Parallel LED Animation Engine for ESP32

**PixelFlow** is a high-performance, non-blocking LED animation engine designed for addressable RGB LED strips (WS2812 / NeoPixel compatible) on ESP32.

It provides:
* Fully parallel per-pixel animations
* A real-time FreeRTOS update loop
* JSON-based animation control
* Group animations (chase / comet / ramp)
* Non-blocking operation (no delay() anywhere)
* Thread-safe API (can be called from any task)

**PixelFlow** is designed as a general-purpose pixel animation engine and is not tied to Adafruit or NeoPixel APIs at the application level.

⸻

### ARCHITECTURE OVERVIEW

Every pixel is an independent state machine.

Each pixel has:
* color (r,g,b)
* base brightness
* current brightness
* animation mode
* animation type
* interval, phase, timers, duration, and end behavior

Pixels run in parallel:
* Pixel 0 can pulse
* Pixel 1–3 can blink
* Pixel 10 can be part of a chase
* Pixel 5–12 can randomize color
* No animation affects any other pixel unless explicitly selected

All animations are non-blocking and time-driven.

A FreeRTOS task updates the engine at about 100Hz.

⸻

### KEY CONCEPT: PIXEL SELECTION IS NOT JSON

**PixelFlow** deliberately separates pixel selection from JSON.

You select which pixels you want using C++:
* int index
* PIXEL_ALL
* {1,3,5} initializer lists

JSON only describes WHAT should happen.

This allows:
* #define, constexpr, enums
* no string building
* no runtime parsing of pixel lists
* full compile-time safety

Example:
```
#define LED_STATUS 3

leds.setPixel(LED_STATUS, “{ "mode":"blink", "interval":200 }”);
```
⸻

## INITIALIZATION

Create and start **PixelFlow**:

**PixelFlow** leds(DATA_PIN, LED_COUNT);
```
void setup()
{
  leds.begin();
  leds.startTask();
}
```
You must call both begin() and startTask().

⸻

## PIXEL SELECTION API

**PixelFlow** supports three ways to select pixels.
1.	Single pixel
leds.setPixel(5, json);
2.	Multiple pixels
leds.setPixel({1,3,7}, json);
3.	All pixels
leds.setPixel(**PixelFlow**::PIXEL_ALL, json);

These work with defines, enums and constants:
```
#define LED_WIFI 2
#define LED_ERROR 7

leds.setPixel({LED_WIFI, LED_ERROR}, “{ "mode":"blink" }”);
```
⸻

## JSON FORMAT

The JSON describes what should happen to the selected pixels.

Supported fields:

```
mode        “off” | “on” | “intensity” | “animate” | “ramp”
type        depends on mode
color       “#RRGGBB” or “random”
r, g, b     numeric color components (optional alternative)
intensity   0..255
interval    milliseconds
duration    milliseconds, 0 = infinite
end         “keep” | “off” | “on”
width       ramp only
```
⸻

## MODES

`mode = “off”`
Turns pixel off immediately.

`mode = “on”`
Turns pixel on at its current base brightness.

`mode = “intensity”`
Sets brightness but does not animate.

`mode = “animate”`
Starts a per-pixel animation (blink, pulse, random).

`mode = “ramp”`
Starts a group chase / comet animation.

⸻

## ANIMATION TYPES (mode=“animate”)

`type = “blink”`
Pixel toggles between on and off.

`type = “pulse”`
Pixel fades in and out using a sine wave.

`type = “random”`
Pixel changes to a random color every interval.

⸻

## EXAMPLES: BASIC PER-PIXEL

Turn pixel 4 on:

`leds.setPixel(4, “{ "mode":"on", "color":"#00FF00" }”);`

Set pixel 2 brightness to 100:

`leds.setPixel(2, “{ "mode":"intensity", "intensity":100 }”);`

Blink LED_STATUS at 200ms:
```
#define LED_STATUS 3

leds.setPixel(LED_STATUS,
“{ "mode":"animate", "type":"blink", "interval":200 }”);
```
Pulse pixels 1,3,5 in blue:
```
leds.setPixel({1,3,5},
“{ "mode":"animate", "type":"pulse", "color":"#0000FF" }”);
```
⸻

## DURATION AND END BEHAVIOR

All animations support duration.

`duration = 0`
Runs forever.

`duration > 0`
Stops after that time.

end defines what happens when the animation ends:

`end = “keep”`
Pixel remains in its last visual state.

`end = “off”`
Pixel turns off.

`end = “on”`
Pixel turns on at full brightness.

Example:

Blink LED for 2 seconds then stay on:
```
leds.setPixel(4,
“{ "mode":"animate", "type":"blink", "interval":100, "duration":2000, "end":"on" }”);
```
⸻

## RAMP / CHASE MODE

Ramp mode creates a moving head with fading tail (comet / chase).

Example:
```
leds.setPixel(**PixelFlow**::PIXEL_ALL,
“{ "mode":"ramp", "type":"up", "width":5, "color":"#FF0000", "interval":40 }”);
```
Fields:

type       “up” or “down”
width      number of pixels in comet
color      “#RRGGBB” or “random”
interval   milliseconds per step
intensity  max brightness
duration   milliseconds (0 = infinite)
end        “keep” | “off” | “on”

Behavior:
* Head fades in
* Middle is full brightness
* Tail fades out
* All other pixels untouched

⸻

## RAMP WITH FIXED PIXEL SET

You can run a ramp only on selected pixels:
```
leds.setPixel({2,5,8,11,14},
“{ "mode":"ramp", "width":3, "color":"random" }”);
```
The ramp runs only across those pixels.

⸻

## FRAGMENTS API

You can supply multiple JSON fragments that are automatically merged.

Example:
```
leds.setPixel(4, {
    “{ "mode":"animate" }”,
    “{ "type":"pulse" }”,
    “{ "color":"#00FF00" }”,
    “{ "interval":300 }”
});
```
This is especially useful when building commands dynamically.

⸻

## THREAD SAFETY

All setPixel calls are mutex-protected.

You can call setPixel from:
* main loop
* WiFi callbacks
* MQTT handlers
* FreeRTOS tasks

**PixelFlow** handles synchronization internally.

⸻

## WHY **PixelFlow** IS DIFFERENT

Traditional LED libraries:
* one animation at a time
* blocking delays
* global state

**PixelFlow**:
* every pixel runs independently
* animations never block
* multiple animations can run simultaneously
* group effects are layered on top

This allows:
* status LEDs
* animations
* progress indicators
* warnings
* UI feedback

all at the same time on one strip.

⸻

## LICENSE

MIT license 
