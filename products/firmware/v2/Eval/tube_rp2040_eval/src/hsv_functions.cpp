/***********************************************************************
 * HSV to RGB Conversion and UART Command Processing
 *
 * Author: Kyopalab. LLC
 * Creation Date: 2025/11/17
 ***********************************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// External references
extern Adafruit_NeoPixel strip;
extern uint8_t current_r, current_g, current_b;
extern float currentHue;
extern float targetHue;
extern void setColor(uint8_t r, uint8_t g, uint8_t b);

// HSV to RGB conversion
// H: 0-360, S: 0-1, V: 0-1
// Output: R, G, B: 0-255
void hsvToRgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
    float c = v * s;
    float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r_prime, g_prime, b_prime;

    if (h >= 0.0f && h < 60.0f) {
        r_prime = c;
        g_prime = x;
        b_prime = 0.0f;
    } else if (h >= 60.0f && h < 120.0f) {
        r_prime = x;
        g_prime = c;
        b_prime = 0.0f;
    } else if (h >= 120.0f && h < 180.0f) {
        r_prime = 0.0f;
        g_prime = c;
        b_prime = x;
    } else if (h >= 180.0f && h < 240.0f) {
        r_prime = 0.0f;
        g_prime = x;
        b_prime = c;
    } else if (h >= 240.0f && h < 300.0f) {
        r_prime = x;
        g_prime = 0.0f;
        b_prime = c;
    } else {  // 300-360
        r_prime = c;
        g_prime = 0.0f;
        b_prime = x;
    }

    r = (uint8_t)((r_prime + m) * 255.0f);
    g = (uint8_t)((g_prime + m) * 255.0f);
    b = (uint8_t)((b_prime + m) * 255.0f);
}

// Set color using HSV values (all LEDs same color)
void setColorHSV(float hue, float saturation, float value) {
    uint8_t r, g, b;
    hsvToRgb(hue, saturation, value, r, g, b);
    setColor(r, g, b);
}

// Set gradient across all 144 LEDs based on center HUE
void setGradientHSV(float centerHue, float hueRange) {
    extern Adafruit_NeoPixel strip;
    int ledCount = strip.numPixels();

    for (int i = 0; i < ledCount; i++) {
        // Calculate HUE for this LED
        // Map LED index (0-143) to HUE offset (-hueRange/2 to +hueRange/2)
        float offset = (float(i) / float(ledCount - 1) - 0.5f) * hueRange;
        float ledHue = centerHue + offset;

        // Wrap HUE to 0-360 range
        if (ledHue < 0.0f) ledHue += 360.0f;
        if (ledHue >= 360.0f) ledHue -= 360.0f;

        // Convert HSV to RGB
        uint8_t r, g, b;
        hsvToRgb(ledHue, 1.0f, 1.0f, r, g, b);  // Full saturation and value

        // Set LED color
        strip.setPixelColor(i, strip.Color(r, g, b));
    }

    strip.show();
}

// Process UART commands from ESP32-C6
void processUartCommand(String command) {
    command.toUpperCase();  // Case-insensitive

    if (command.length() == 0) {
        return;
    }

    // Parse "HUE,<value>" format
    if (command.startsWith("HUE,")) {
        String valueStr = command.substring(4);  // Skip "HUE,"
        float hueValue = valueStr.toFloat();

        if (hueValue >= 0.0f && hueValue <= 360.0f) {
            // Set target HUE - smooth transition happens in loop()
            targetHue = hueValue;
            Serial.print("UART HUE target: ");
            Serial.println(targetHue);
        } else {
            Serial.print("Error: HUE value must be 0-360, got: ");
            Serial.println(hueValue);
        }
    }
}
