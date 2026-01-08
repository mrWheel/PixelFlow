#include <Arduino.h>
#include <PixelFlow.h>

using pixelFlow::PixelFlow;

// ==========================
// Hardware configuration
// ==========================
static constexpr uint8_t  LED_PIN   = 19;  // GPIO pin waar de pixels op aangesloten zijn
static constexpr uint16_t LED_COUNT = 10;   // max 10 pixels: 0..9

PixelFlow leds(LED_PIN, LED_COUNT);

// Helper: start "random" animation on all 10 pixels
static void startRandomAll()
{
  Serial.println("Starting random animation on all pixels for 4 seconds...");
  leds.setPixel(PixelFlow::PIXEL_ALL,
      "{ \"mode\":\"animate\", \"type\":\"random\", \"interval\":250, \"intensity\":180, \"duration\":4000, \"end\":\"off\" }");
  Serial.println("Setting pixel 5 ON PURPLE for 10 seconds...");
  leds.setPixel(5,
      "{ \"mode\":\"on\", \"intensity\":120, \"color\":\"#0FFFF0\", \"duration\":10000 }");
  Serial.println("Setting All pixels to ON GREEN for 2 seconds...");
  leds.setPixel(PixelFlow::PIXEL_ALL,
      "{ \"mode\":\"on\", \"intensity\":120, \"color\":\"#00FF00\", \"duration\":2000, \"end\":\"off\" }");
}

// Helper: demonstrate one mode on a specific pixel (index 0..9)
static void demoPixelMode(uint8_t idx)
{
  switch (idx)
  {
    case 0:
      Serial.println("Switch pixel " + String(idx) + " ON (green)");
      leds.setPixel(idx, "{ \"mode\":\"on\", \"color\":\"#00FF00\", \"intensity\":160 }");
      break;

    case 1:
      Serial.println("Switch pixel " + String(idx) + " OFF");  // OFF
      leds.setPixel(idx, "{ \"mode\":\"off\" }");
      break;

    case 2:
      // INTENSITY (dim white)
      Serial.println("Set pixel " + String(idx) + " to INTENSITY (dim white)");
      leds.setPixel(idx, "{ \"mode\":\"intensity\", \"color\":\"#FFFFFF\", \"intensity\":40 }");
      break;

    case 3:
      // ANIMATE: BLINK (red)
      Serial.println("Set pixel " + String(idx) + " to ANIMATE: BLINK (red)");
      leds.setPixel(idx, "{ \"mode\":\"animate\", \"type\":\"blink\", \"color\":\"#FF0000\", \"interval\":150, \"duration\":0, \"end\":\"keep\" }");
      break;

    case 4:
      // ANIMATE: PULSE (groen)
      Serial.println("Set pixel " + String(idx) + " to ANIMATE: PULSE (green)");
      leds.setPixel(idx, "{ \"mode\":\"animate\", \"type\":\"pulse\", \"color\":\"#00FF00\", \"interval\":150, \"duration\":0, \"end\":\"keep\" }");
      break;

    case 5:
      // ANIMATE: PULSE (blue)
      Serial.println("Set pixel " + String(idx) + " to ANIMATE: PULSE (blue)");
      leds.setPixel(idx, "{ \"mode\":\"animate\", \"type\":\"pulse\", \"color\":\"#0000FF\", \"interval\":150, \"duration\":0, \"end\":\"keep\" }");
      break;

    case 6:
      // ANIMATE: RANDOM (fast)
      Serial.println("Set pixel " + String(idx) + " to ANIMATE: RANDOM (fast)");
      leds.setPixel(idx, "{ \"mode\":\"animate\", \"type\":\"random\", \"interval\":80, \"intensity\":200, \"duration\":0, \"end\":\"keep\" }");
      break;

    case 7:
      // RAMP (up) across a subset (0..9) doesn't make sense per-pixel, so we demo a short ramp on ALL
      // but to keep the "per pixel 0-9" story, we trigger it when idx==6.
      Serial.println("Set ALL pixels to RAMP UP (gray) for 2.5 seconds");
      leds.setPixel(PixelFlow::PIXEL_ALL,
          "{ \"mode\":\"ramp\", \"type\":\"up\", \"width\":4, \"color\":\"#202020\", \"interval\":35, \"intensity\":180, \"duration\":2500, \"end\":\"keep\" }");
      break;

    case 8:
      // ON (purple)
      Serial.println("Switch pixel " + String(idx) + " ON (purple)");
      leds.setPixel(idx, "{ \"mode\":\"on\", \"color\":\"#8000FF\", \"intensity\":180 }");
      break;

    case 9:
      // OFF then ON after a short duration-like behavior: do a blink that ends OFF
      Serial.println("Set pixel " + String(idx) + " to BLINK that ends ON");
      leds.setPixel(idx, "{ \"mode\":\"animate\", \"type\":\"blink\", \"color\":\"#FFFFFF\", \"interval\":120, \"duration\":1200, \"end\":\"on\" }");
      break;

    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  while(!Serial) { delay(1000); }  // wacht op seriële poort
  delay(2000);
  Serial.println("\nPixelFlow Basic Example");

  leds.begin();
  leds.startTask();

  // Start with random across all 10 pixels
  startRandomAll();
  Serial.print("Now wait for 10 seconds ...");
  delay(1000);
  Serial.print("10...");
  delay(1000);
  Serial.print("9...");
  delay(1000);
  Serial.print("8...");
  delay(1000);
  Serial.print("7...");
  delay(1000);
  Serial.print("6...");
  delay(1000);
  Serial.print("5...");
  delay(1000);
  Serial.print("4...");
  delay(1000);
  Serial.print("3...");
  delay(1000);
  Serial.print("2...");
  delay(1000);
  Serial.println("1...");
  delay(1000);
  Serial.println("Starting demo sequence!\n");
} // setupsetup()

void loop()
{
  // Timeline:
  // 0) Start with random all (already running)
  // 1) Every few seconds, show a specific mode demo for pixel 0..9 (one at a time)
  // 2) After pixel 9 demo, return to random all and repeat

  static uint32_t phaseStart = 0;
  static uint8_t  phase = 0;

  // Each phase lasts this long
  static constexpr uint32_t PHASE_MS = 2500;

  const uint32_t now = millis();

  if (phaseStart == 0)
  {
    phaseStart = now;
    phase = 0;
  }

  if (now - phaseStart < PHASE_MS)
  {
    return;
  }

  phaseStart = now;

  // phase 0..9 -> demo pixel mode for that index
  // phase 10 -> go back to random all
  if (phase <= 9)
  {
    Serial.printf("Demo phase: pixel %u\n", phase);
    demoPixelMode(phase);
    phase++;
    return;
  }

  // Reset to random across all 10 pixels
  Serial.println("\n\nBack to random (all pixels)");
  startRandomAll();
  phase = 0;
}
