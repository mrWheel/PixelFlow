
#include "PixelFlow.h"

namespace pixelFlow {

//-- LET OP: versie nummer ook opnemen in:
//-- library.json
//-- library.properties
const char* PROG_VERSION = "v1.2.0";

/************************************************************
    PixelFlow Animation Engine (Parallel, Non-Blocking)
    -------------------------------------------------------
    Implementatiebestand (.cpp)

    - Per-pixel state machine voor OFF/ON/INTENSITY/ANIMATE
    - Groeps-state machine voor RAMP (chase/comet)
    - Non-blocking update() in FreeRTOS task
    - setPixel() is mutex-beveiligd
************************************************************/

PixelFlow::PixelFlow(uint8_t pin, uint16_t count)
    : pin(pin),
      numPixels(count),
      strip(count, pin, NEO_GRB + NEO_KHZ800),
      mutexHandle(nullptr),
      taskHandle(nullptr)
{
    pixels.resize(numPixels);
    mutexHandle = xSemaphoreCreateMutex();
}

void PixelFlow::begin()
{
    strip.begin();
    strip.show();
}

void PixelFlow::startTask(const char *name,
                         uint16_t stack,
                         uint8_t prio,
                         uint8_t core)
{
    xTaskCreatePinnedToCore(
        taskEntry,
        name,
        stack,
        this,
        prio,
        &taskHandle,
        core
    );
}

void PixelFlow::taskEntry(void *param)
{
    PixelFlow *self = static_cast<PixelFlow*>(param);

    for (;;)
    {
        self->update();
        vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz
    }
}

PixelMode PixelFlow::toMode(const char *s)
{
    if (!s) return PixelMode::OFF;

    if (!strcasecmp(s, "off")) return PixelMode::OFF;
    if (!strcasecmp(s, "on")) return PixelMode::ON;
    if (!strcasecmp(s, "intensity")) return PixelMode::INTENSITY;
    if (!strcasecmp(s, "animate")) return PixelMode::ANIMATE;
    if (!strcasecmp(s, "ramp")) return PixelMode::RAMP;

    return PixelMode::OFF;
}

PixelAnimType PixelFlow::toAnim(const char *s)
{
    if (!s) return PixelAnimType::NONE;

    if (!strcasecmp(s, "blink")) return PixelAnimType::BLINK;
    if (!strcasecmp(s, "pulse")) return PixelAnimType::PULSE;
    if (!strcasecmp(s, "random")) return PixelAnimType::RANDOM_COLOR;

    return PixelAnimType::NONE;
}

RampDirection PixelFlow::toRampDirection(const char *s)
{
    if (!s) return RampDirection::UP;

    if (!strcasecmp(s, "down")) return RampDirection::DOWN;
    return RampDirection::UP;
}

EndBehavior PixelFlow::toEndBehavior(const char *s)
{
    if (!s) return EndBehavior::KEEP;

    if (!strcasecmp(s, "off")) return EndBehavior::OFF;
    if (!strcasecmp(s, "on")) return EndBehavior::ON;
    return EndBehavior::KEEP;
}

void PixelFlow::parseColor(JsonVariantConst col, PixelState &p)
{
    const char *c = col.as<const char *>();
    if (!c || strlen(c) != 7 || c[0] != '#')
    {
        return;
    }

    char rs[3] = { c[1], c[2], 0 };
    char gs[3] = { c[3], c[4], 0 };
    char bs[3] = { c[5], c[6], 0 };

    p.r = strtol(rs, nullptr, 16);
    p.g = strtol(gs, nullptr, 16);
    p.b = strtol(bs, nullptr, 16);
}

/************************************************************
    Target selectie helpers

    Deze helpers vertalen de pixel selector naar een lijst met
    indices. Alle bounds checks gebeuren hier, zodat de rest
    van de code gewoon met targets kan werken.
************************************************************/

std::vector<int> PixelFlow::makeTargets(int pixel) const
{
    std::vector<int> targets;

    if (pixel == PIXEL_ALL)
    {
        targets.reserve(numPixels);
        for (int i = 0; i < (int)numPixels; ++i)
        {
            targets.push_back(i);
        }
        return targets;
    }

    if (pixel >= 0 && pixel < (int)numPixels)
    {
        targets.push_back(pixel);
    }

    return targets;
}

std::vector<int> PixelFlow::makeTargets(std::initializer_list<int> list) const
{
    std::vector<int> targets;
    targets.reserve(list.size());

    for (int idx : list)
    {
        if (idx >= 0 && idx < (int)numPixels)
        {
            targets.push_back(idx);
        }
    }

    return targets;
}

/************************************************************
    JSON fragment merge helper

    Maakt het mogelijk om meerdere JSON snippets aan te leveren
    die samengevoegd worden in één document.
************************************************************/

bool PixelFlow::mergeFragmentsIntoDoc(JsonDocument &doc, std::initializer_list<const char *> fragments)
{
    for (auto &frag : fragments)
    {
        JsonDocument part;
        auto err = deserializeJson(part, frag);
        if (err)
        {
            Serial.printf("JSON fragment error: %s -> %s\n",
                          frag, err.c_str());
            continue;
        }

        for (JsonPairConst kv : part.as<JsonObjectConst>())
        {
            doc[kv.key()] = kv.value();
        }
    }
    return true;
}

/************************************************************
    Backward compatibility: targets uit doc["pixel"]

    Ondersteunt:
        - ontbrekend of "all" -> alle pixels
        - int -> één pixel
        - array -> lijst

    Let op: deze functie bestaat zodat oudere code met "pixel"
    in JSON blijft werken.
************************************************************/

std::vector<int> PixelFlow::targetsFromDocPixelField(JsonDocument &doc) const
{
    std::vector<int> targets;

    JsonVariantConst pv = doc["pixel"];

    if (pv.isNull() || (pv.is<const char*>() && !strcasecmp(pv.as<const char*>(), "all")))
    {
        targets.reserve(numPixels);
        for (int i = 0; i < (int)numPixels; ++i)
        {
            targets.push_back(i);
        }
        return targets;
    }

    if (pv.is<int>())
    {
        int idx = pv.as<int>();
        if (idx >= 0 && idx < (int)numPixels)
        {
            targets.push_back(idx);
        }
        return targets;
    }

    if (pv.is<JsonArrayConst>())
    {
        for (JsonVariantConst v : pv.as<JsonArrayConst>())
        {
            int idx = v.as<int>();
            if (idx >= 0 && idx < (int)numPixels)
            {
                targets.push_back(idx);
            }
        }
        return targets;
    }

    return targets;
}

/************************************************************
    applyJsonToTargets()

    Past de instellingen in doc toe op een set pixels (targets).
    Deze functie is de kern van setPixel(...).

    - Mode "ramp" is een groepsanimatie: configureRampFromJson()
    - Overige modes passen PixelState per pixel aan
************************************************************/

/************************************************************
    isPixelBlocked()

    Check if pixel is currently blocked by an active animation
    with duration > 0 that hasn't expired yet.
************************************************************/

bool PixelFlow::isPixelBlocked(int idx, uint32_t now) const
{
  if (idx < 0 || idx >= (int)numPixels)
  {
    return false;
  }

  const PixelState &p = pixels[idx];

  //-- Any mode with duration > 0 blocks new commands
  if (p.animDurationMs > 0)
  {
    //-- Check if duration hasn't expired yet
    if (now - p.animStartTime < p.animDurationMs)
    {
      return true;
    }
  }

  return false;
}

void PixelFlow::applyJsonToTargets(JsonDocument &doc, const std::vector<int> &targets)
{
    if (targets.empty())
    {
        return;
    }

    const char *modeStr = doc["mode"].is<const char*>() ? doc["mode"].as<const char*>() : nullptr;
    PixelMode mode = toMode(modeStr);

    // RAMP is een speciale groeps-animatie
    if (mode == PixelMode::RAMP)
    {
        configureRampFromJson(doc, targets);
        return;
    }

    // Voor elke pixel in targets: per-pixel animatie / state
    uint32_t now = millis();

    for (int idx : targets)
    {
        if (idx < 0 || idx >= (int)numPixels)
        {
            continue;
        }

        PixelState &p = pixels[idx];

        //-- Check if pixel is blocked by active duration-based animation
        if (isPixelBlocked(idx, now))
        {
            //-- Store command in pending, overwriting any previous pending command
            p.pending.active = true;

            //-- Store mode if provided in JSON, else keep current
            if (modeStr != nullptr)
            {
                p.pending.mode = mode;
            }
            else
            {
                p.pending.mode = p.mode;
            }

            //-- Store animation type
            if (doc["type"].is<const char*>())
            {
                p.pending.animType = toAnim(doc["type"].as<const char*>());
            }
            else
            {
                p.pending.animType = p.animType;
            }

            //-- Store color
            if (doc["color"].is<const char*>())
            {
                PixelState tmp;
                parseColor(doc["color"], tmp);
                p.pending.r = tmp.r;
                p.pending.g = tmp.g;
                p.pending.b = tmp.b;
            }
            else
            {
                if (doc["r"].is<int>())
                {
                    p.pending.r = doc["r"].as<int>();
                }
                else
                {
                    p.pending.r = p.r;
                }

                if (doc["g"].is<int>())
                {
                    p.pending.g = doc["g"].as<int>();
                }
                else
                {
                    p.pending.g = p.g;
                }

                if (doc["b"].is<int>())
                {
                    p.pending.b = doc["b"].as<int>();
                }
                else
                {
                    p.pending.b = p.b;
                }
            }

            //-- Store intensity
            if (doc["intensity"].is<int>())
            {
                p.pending.baseBrightness = doc["intensity"].as<int>();
            }
            else
            {
                p.pending.baseBrightness = p.baseBrightness;
            }

            //-- Store interval
            if (doc["interval"].is<int>())
            {
                p.pending.intervalMs = doc["interval"].as<int>();
            }
            else
            {
                p.pending.intervalMs = p.intervalMs;
            }

            //-- Store duration
            if (doc["duration"].is<int>())
            {
                p.pending.animDurationMs = doc["duration"].as<int>();
            }
            else
            {
                p.pending.animDurationMs = 0;
            }

            //-- Store end behavior
            if (doc["end"].is<const char*>())
            {
                p.pending.endBehavior = toEndBehavior(doc["end"].as<const char*>());
            }
            else
            {
                p.pending.endBehavior = p.endBehavior;
            }

            //-- Skip applying the command, continue to next pixel
            continue;
        }

        // Als we geen mode in JSON hebben, behoud mode
        if (modeStr != nullptr)
        {
            p.mode = mode;
        }

        if (doc["type"].is<const char*>())
        {
            p.animType = toAnim(doc["type"].as<const char*>());
        }

        // Kleur
        if (doc["color"].is<const char*>())
        {
            parseColor(doc["color"], p);
        }
        else
        {
            if (doc["r"].is<int>())
            {
                p.r = doc["r"].as<int>();
            }
            if (doc["g"].is<int>())
            {
                p.g = doc["g"].as<int>();
            }
            if (doc["b"].is<int>())
            {
                p.b = doc["b"].as<int>();
            }
        }

        if (doc["intensity"].is<int>())
        {
            p.baseBrightness = doc["intensity"].as<int>();
        }

        if (doc["interval"].is<int>())
        {
            p.intervalMs = doc["interval"].as<int>();
        }

        // Duration & end
        if (doc["duration"].is<int>())
        {
            p.animDurationMs = doc["duration"].as<int>();
            p.animStartTime = now;
        }
        else if (p.mode == PixelMode::ANIMATE)
        {
            // default anim: oneindig
            p.animDurationMs = 0;
            p.animStartTime = now;
        }

        if (doc["end"].is<const char*>())
        {
            p.endBehavior = toEndBehavior(doc["end"].as<const char*>());
        }

        // Mode gedrag
        switch (p.mode)
        {
            case PixelMode::OFF:
                p.animType = PixelAnimType::NONE;
                p.currentBrightness = 0;
                p.animDurationMs = 0;
                break;

            case PixelMode::ON:
            case PixelMode::INTENSITY:
                p.animType = PixelAnimType::NONE;
                p.currentBrightness = p.baseBrightness;
                p.animDurationMs = 0;
                break;

            case PixelMode::ANIMATE:
                p.toggleState = false;
                p.phase = 0.0f;
                p.lastUpdate = now;
                break;

            default:
                break;
        }
    }
}

/************************************************************
    setPixel() - fragment variants

    Hiermee kun je meerdere JSON snippets aanbieden die
    samengevoegd worden tot één config.
************************************************************/

void PixelFlow::setPixel(std::initializer_list<const char*> fragments)
{
    JsonDocument merged;
    mergeFragmentsIntoDoc(merged, fragments);

    std::string json;
    serializeJson(merged, json);
    setPixel(json);
}

void PixelFlow::setPixel(int pixel, std::initializer_list<const char*> fragments)
{
    JsonDocument merged;
    mergeFragmentsIntoDoc(merged, fragments);

    std::string json;
    serializeJson(merged, json);
    setPixel(pixel, json);
}

void PixelFlow::setPixel(std::initializer_list<int> px, std::initializer_list<const char*> fragments)
{
    JsonDocument merged;
    mergeFragmentsIntoDoc(merged, fragments);

    std::string json;
    serializeJson(merged, json);
    setPixel(px, json);
}

/************************************************************
    setPixel() - string variants

    - Variant zonder expliciete selector blijft bestaan voor
      compatibiliteit en leest optioneel doc["pixel"].
    - Varianten met selector gebruiken de C++ selectie en
      verwachten dat JSON géén pixel selectie hoeft te bevatten.
************************************************************/

void PixelFlow::setPixel(const std::string &json)
{
    JsonDocument doc;
    auto err = deserializeJson(doc, json);

    if (err)
    {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
    }

    MutexLock lock(mutexHandle);

    std::vector<int> targets = targetsFromDocPixelField(doc);
    applyJsonToTargets(doc, targets);
}

void PixelFlow::setPixel(int pixel, const std::string &json)
{
    JsonDocument doc;
    auto err = deserializeJson(doc, json);

    if (err)
    {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
    }

    MutexLock lock(mutexHandle);

    std::vector<int> targets = makeTargets(pixel);
    applyJsonToTargets(doc, targets);
}

void PixelFlow::setPixel(std::initializer_list<int> px, const std::string &json)
{
    JsonDocument doc;
    auto err = deserializeJson(doc, json);

    if (err)
    {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return;
    }

    MutexLock lock(mutexHandle);

    std::vector<int> targets = makeTargets(px);
    applyJsonToTargets(doc, targets);
}

void PixelFlow::configureRampFromJson(JsonDocument &doc, const std::vector<int> &targets)
{
    if (targets.empty())
    {
        return;
    }

    ramp.active = true;
    ramp.indices = targets;

    const char *typeStr = doc["type"].is<const char*>() ? doc["type"].as<const char*>() : "up";
    ramp.direction = toRampDirection(typeStr);

    ramp.width = doc["width"].is<int>() ? (uint8_t)doc["width"].as<int>() : 3;
    if (ramp.width < 1)
    {
        ramp.width = 1;
    }
    if (ramp.width > ramp.indices.size())
    {
        ramp.width = ramp.indices.size();
    }

    ramp.maxIntensity = doc["intensity"].is<int>() ?
                        (uint8_t)doc["intensity"].as<int>() : 200;

    ramp.intervalMs = doc["interval"].is<int>() ?
                      (uint16_t)doc["interval"].as<int>() : 30;

    ramp.durationMs = doc["duration"].is<int>() ?
                      (uint32_t)doc["duration"].as<int>() : 0;

    const char *endStr = doc["end"].is<const char*>() ? doc["end"].as<const char*>() : "keep";
    ramp.endBehavior = toEndBehavior(endStr);

    // kleur
    ramp.randomPerPixel = true;
    if (doc["color"].is<const char*>())
    {
        const char *c = doc["color"].as<const char*>();
        if (!strcasecmp(c, "random"))
        {
            ramp.randomPerPixel = true;
        }
        else
        {
            PixelState tmp;
            parseColor(doc["color"], tmp);
            ramp.randomPerPixel = false;
            ramp.fixedR = tmp.r;
            ramp.fixedG = tmp.g;
            ramp.fixedB = tmp.b;
        }
    }

    // startpositie en voortgangsrichting (wrap, geen bounce meer)
    int count = (int)ramp.indices.size();

    if (ramp.direction == RampDirection::UP)
    {
        ramp.headIndex = 0;          // begin bij laagste index
        ramp.stepDirection = +1;     // altijd vooruit
    }
    else
    {
        ramp.headIndex = count - 1;  // begin bij hoogste index
        ramp.stepDirection = -1;     // altijd achteruit
    }

    uint32_t now = millis();
    ramp.startTime = now;
    ramp.lastStepTime = now;

    Serial.printf("Ramp configured: pixels=%d, width=%d, interval=%u, duration=%u, dir=%s\n",
                  count,
                  (int)ramp.width,
                  (unsigned)ramp.intervalMs,
                  (unsigned)ramp.durationMs,
                  (ramp.direction == RampDirection::UP ? "up" : "down"));
}

void PixelFlow::stepRamp(uint32_t now)
{
    if (!ramp.active)
    {
        return;
    }

    int count = (int)ramp.indices.size();
    if (count == 0)
    {
        ramp.active = false;
        return;
    }

    // Duration check
    if (ramp.durationMs > 0 && (now - ramp.startTime >= ramp.durationMs))
    {
        // Animatie beëindigen
        switch (ramp.endBehavior)
        {
            case EndBehavior::KEEP:
                for (int idx : ramp.indices)
                {
                    PixelState &p = pixels[idx];
                    p.mode = PixelMode::INTENSITY;
                    p.animType = PixelAnimType::NONE;
                    p.animDurationMs = 0;
                }
                break;

            case EndBehavior::OFF:
                for (int idx : ramp.indices)
                {
                    PixelState &p = pixels[idx];
                    p.mode = PixelMode::OFF;
                    p.animType = PixelAnimType::NONE;
                    p.currentBrightness = 0;
                    p.animDurationMs = 0;
                }
                break;

            case EndBehavior::ON:
                for (int idx : ramp.indices)
                {
                    PixelState &p = pixels[idx];
                    p.mode = PixelMode::ON;
                    p.animType = PixelAnimType::NONE;
                    p.baseBrightness = ramp.maxIntensity;
                    p.currentBrightness = ramp.maxIntensity;
                    p.animDurationMs = 0;
                }
                break;
        }

        ramp.active = false;
        return;
    }

    if (now - ramp.lastStepTime < ramp.intervalMs)
    {
        return;
    }

    ramp.lastStepTime = now;

    // Zet alle betrokken pixels eerst op 0 brightness (binnen ramp)
    for (int idx : ramp.indices)
    {
        PixelState &p = pixels[idx];
        // Alleen de ramp-animatie beïnvloedt brightness; mode markeren als RAMP
        p.mode = PixelMode::RAMP;
        p.currentBrightness = 0;
    }

    // HEAD verschuiven: altijd wrap, geen bounce
    if (ramp.direction == RampDirection::UP)
    {
        ramp.headIndex++;
        if (ramp.headIndex >= count)
        {
            ramp.headIndex = 0;      // wrap terug naar "laag"
        }
    }
    else // RampDirection::DOWN
    {
        ramp.headIndex--;
        if (ramp.headIndex < 0)
        {
            ramp.headIndex = count - 1;  // wrap terug naar "hoog"
        }
    }

    // Width-zone opbouwen rondom headIndex
    for (int offset = 0; offset < (int)ramp.width; ++offset)
    {
        // offset 0 = head, offset 1,2,... = tail
        int pos = ramp.headIndex - offset * ramp.stepDirection;

        // wrap-around binnen de index-lijst
        while (pos < 0)
        {
            pos += count;
        }
        while (pos >= count)
        {
            pos -= count;
        }

        int pixelIndex = ramp.indices[pos];
        if (pixelIndex < 0 || pixelIndex >= (int)numPixels)
        {
            continue;
        }

        PixelState &p = pixels[pixelIndex];

        float t;
        if (ramp.width == 1)
        {
            t = 1.0f;
        }
        else
        {
            // head = 1.0 → tail = 0.0
            t = 1.0f - (float)offset / (float)(ramp.width - 1);
        }

        float factor = sinf(t * (PI / 2.0f)); // 0..1
        uint8_t br = (uint8_t)(factor * ramp.maxIntensity);

        p.currentBrightness = br;

        if (br > 0)
        {
            if (ramp.randomPerPixel)
            {
                p.r = random(256);
                p.g = random(256);
                p.b = random(256);
            }
            else
            {
                p.r = ramp.fixedR;
                p.g = ramp.fixedG;
                p.b = ramp.fixedB;
            }
        }
    }
}

void PixelFlow::update()
{
    uint32_t now = millis();

    MutexLock lock(mutexHandle);

    // 1. Per-pixel modes (duration check for all modes)
    for (int i = 0; i < (int)numPixels; ++i)
    {
        PixelState &p = pixels[i];

        //-- RAMP mode pixels worden in stepRamp() behandeld
        if (p.mode == PixelMode::RAMP)
        {
            continue;
        }

        //-- Duration check for any mode with active duration
        if (p.animDurationMs > 0 && (now - p.animStartTime >= p.animDurationMs))
        {
            //-- Check if there is a pending command to execute
            if (p.pending.active)
            {
                //-- Apply the pending command
                p.mode = p.pending.mode;
                p.animType = p.pending.animType;
                p.r = p.pending.r;
                p.g = p.pending.g;
                p.b = p.pending.b;
                p.baseBrightness = p.pending.baseBrightness;
                p.intervalMs = p.pending.intervalMs;
                p.animDurationMs = p.pending.animDurationMs;
                p.endBehavior = p.pending.endBehavior;

                //-- Reset pending flag
                p.pending.active = false;

                //-- Initialize animation state based on mode
                switch (p.mode)
                {
                    case PixelMode::OFF:
                        p.animType = PixelAnimType::NONE;
                        p.currentBrightness = 0;
                        p.animDurationMs = 0;
                        break;

                    case PixelMode::ON:
                    case PixelMode::INTENSITY:
                        p.animType = PixelAnimType::NONE;
                        p.currentBrightness = p.baseBrightness;
                        p.animDurationMs = 0;
                        break;

                    case PixelMode::ANIMATE:
                        p.toggleState = false;
                        p.phase = 0.0f;
                        p.lastUpdate = now;
                        p.animStartTime = now;
                        break;

                    default:
                        break;
                }
            }
            else
            {
                //-- No pending command, apply end behavior
                switch (p.endBehavior)
                {
                    case EndBehavior::KEEP:
                        p.mode = PixelMode::INTENSITY;
                        p.animType = PixelAnimType::NONE;
                        p.animDurationMs = 0;
                        break;

                    case EndBehavior::OFF:
                        p.mode = PixelMode::OFF;
                        p.animType = PixelAnimType::NONE;
                        p.currentBrightness = 0;
                        p.animDurationMs = 0;
                        break;

                    case EndBehavior::ON:
                        p.mode = PixelMode::ON;
                        p.animType = PixelAnimType::NONE;
                        p.currentBrightness = p.baseBrightness;
                        p.animDurationMs = 0;
                        break;
                }
            }
            continue;
        }

        //-- Animation updates only for ANIMATE mode
        if (p.mode != PixelMode::ANIMATE)
        {
            continue;
        }

        if (now - p.lastUpdate < p.intervalMs)
        {
            continue;
        }

        p.lastUpdate = now;

        switch (p.animType)
        {
            case PixelAnimType::BLINK:
                p.toggleState = !p.toggleState;
                p.currentBrightness = p.toggleState ? p.baseBrightness : 0;
                break;

            case PixelAnimType::PULSE:
            {
                // fase op basis van tijd, sinus
                float dt = (float)p.intervalMs / 1000.0f;
                p.phase += dt * 2.0f * PI / 2.0f; // vrij kleine toename
                if (p.phase > 2.0f * PI)
                {
                    p.phase -= 2.0f * PI;
                }

                float s = (sinf(p.phase) + 1.0f) * 0.5f;
                p.currentBrightness = (uint8_t)(s * p.baseBrightness);
                break;
            }

            case PixelAnimType::RANDOM_COLOR:
                p.r = random(256);
                p.g = random(256);
                p.b = random(256);
                p.currentBrightness = p.baseBrightness;
                break;

            default:
                break;
        }
    }

    // 2. RAMP animatie (groeps-chase)
    stepRamp(now);

    // 3. Alles naar strip sturen
    applyStateToStrip();
}

void PixelFlow::applyStateToStrip()
{
    for (int i = 0; i < (int)numPixels; ++i)
    {
        PixelState &p = pixels[i];

        uint8_t r = (p.r * p.currentBrightness) / 255;
        uint8_t g = (p.g * p.currentBrightness) / 255;
        uint8_t b = (p.b * p.currentBrightness) / 255;

        strip.setPixelColor(i, strip.Color(r, g, b));
    }

    strip.show();
}

} // namespace pixelFlow
