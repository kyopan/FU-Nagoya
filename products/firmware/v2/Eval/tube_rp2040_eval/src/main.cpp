/***********************************************************************
 *   ________  ______  ______   __  _____
 *  /_  __/ / / / __ )/ ____/  / / / /__ \
 *   / / / / / / __  / __/    / / / /__/ /
 *  / / / /_/ / /_/ / /___   / /_/ // __/
 * /_/  \____/_____/_____/   \____//____/
 *
 * FU Tube v2 - RP2040 Eval
 * Serial RGB Color Control for Testing
 *
 * Author: Kyopalab. LLC
 * Creation Date: 2025/11/17
 * Version: 0.1-eval
 * License: Proprietary License
 * MCU: Waveshare RP2040-Zero
 *
 * Commands:
 *   R<0-255>    - Set Red value
 *   G<0-255>    - Set Green value
 *   B<0-255>    - Set Blue value
 *   C<R>,<G>,<B> - Set all RGB at once (e.g., "C255,100,0")
 *   H           - Help (show commands)
 *   ?           - Show current color
 *
 * Example:
 *   R255        -> Red = 255
 *   G128        -> Green = 128
 *   B0          -> Blue = 0
 *   C255,0,255  -> Magenta
 ************************************************************************/

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// FU v2 Eval Board - NeoPixel LED Strip
#define PIN_NEOPIXEL    29  // GPIO 29 (based on fu_v2_eval_board.ino)
#define NEOPIXEL_COUNT  144 // 72 LEDs × 2 strips (24×2 + 24×2 + 24×2 = 144)
#define NEOPIXEL_POWER  -1  // No separate power control

// UART pins for communication with ESP32-C6 (U1)
// RP2040-Zero Serial1: GPIO 0 (TX), GPIO 1 (RX)
#define PIN_UART_TX     0
#define PIN_UART_RX     1

// Create NeoPixel object
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// Current RGB values
uint8_t current_r = 0;
uint8_t current_g = 0;
uint8_t current_b = 0;

// Serial input buffer (USB Serial)
String inputString = "";
bool stringComplete = false;

// UART input buffer (from ESP32-C6)
String uartInputString = "";
bool uartStringComplete = false;

// Current HUE value for gradation
float currentHue = 220.0f;
float targetHue = 220.0f;  // Target HUE to transition to
const float HUE_TRANSITION_SPEED = 1.5f;  // Degrees per frame (~60fps = smooth transition)

// Function prototypes
void updateLED();
void processSerialCommand(String command);
void processUartCommand(String command);
void printHelp();
void printCurrentColor();
void setColor(uint8_t r, uint8_t g, uint8_t b);
void setColorHSV(float hue, float saturation, float value);
void setGradientHSV(float centerHue, float hueRange);
void hsvToRgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b);

void setup() {
    // Serial initialization (USB)
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("FU Tube v2 - RP2040 Eval");
    Serial.println("UART HUE Gradation + Serial RGB Control");
    Serial.println("Waveshare RP2040-Zero");
    Serial.println("========================================\n");

    // UART initialization (from ESP32-C6)
    Serial1.begin(9600);
    Serial.println("UART initialized at 9600 baud (GPIO0 TX, GPIO1 RX)");

    // NeoPixel initialization
    strip.begin();
    strip.setBrightness(30);   // 13/255 brightness (~5%)
    strip.show();              // Initialize all pixels to OFF

    Serial.print("NeoPixel strip initialized on GPIO ");
    Serial.println(PIN_NEOPIXEL);
    Serial.print("LED count: ");
    Serial.println(NEOPIXEL_COUNT);
    printHelp();
    Serial.println("\nReady for commands...\n");

    // Set initial gradient to elegant blue-purple (HUE 220°)
    setGradientHSV(220.0f, 40.0f);
}

void loop() {
    // Check for USB Serial input (manual commands)
    while (Serial.available()) {
        char inChar = (char)Serial.read();

        if (inChar == '\n' || inChar == '\r') {
            if (inputString.length() > 0) {
                stringComplete = true;
            }
        } else {
            inputString += inChar;
        }
    }

    // Process complete USB Serial command
    if (stringComplete) {
        inputString.trim();  // Remove whitespace
        processSerialCommand(inputString);
        inputString = "";
        stringComplete = false;
    }

    // Check for UART input (from ESP32-C6)
    while (Serial1.available()) {
        char inChar = (char)Serial1.read();

        if (inChar == '\n' || inChar == '\r') {
            if (uartInputString.length() > 0) {
                uartStringComplete = true;
            }
        } else {
            uartInputString += inChar;
        }
    }

    // Process complete UART command
    if (uartStringComplete) {
        uartInputString.trim();  // Remove whitespace
        Serial.print("UART received: ");
        Serial.println(uartInputString);
        processUartCommand(uartInputString);
        uartInputString = "";
        uartStringComplete = false;
    }

    // Smooth HUE transition - gradually move currentHue toward targetHue
    if (fabs(currentHue - targetHue) > 0.1f) {
        // Calculate shortest path on color wheel (0-360° wraps around)
        float diff = targetHue - currentHue;

        // Normalize difference to -180 to +180 range (shortest path)
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;

        // Move toward target by step size
        if (fabs(diff) < HUE_TRANSITION_SPEED) {
            currentHue = targetHue;  // Close enough, snap to target
        } else {
            currentHue += (diff > 0) ? HUE_TRANSITION_SPEED : -HUE_TRANSITION_SPEED;
        }

        // Wrap currentHue to 0-360 range
        if (currentHue < 0.0f) currentHue += 360.0f;
        if (currentHue >= 360.0f) currentHue -= 360.0f;

        // Update LED gradient with new interpolated HUE
        setGradientHSV(currentHue, 40.0f);
    }
}

void updateLED() {
    // Set all LEDs to the same color
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        strip.setPixelColor(i, strip.Color(current_r, current_g, current_b));
    }
    strip.show();
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
    current_r = r;
    current_g = g;
    current_b = b;
    updateLED();
    Serial.print("Color set to RGB(");
    Serial.print(r);
    Serial.print(", ");
    Serial.print(g);
    Serial.print(", ");
    Serial.print(b);
    Serial.println(")");
}

void processSerialCommand(String command) {
    command.toUpperCase();  // Case-insensitive

    if (command.length() == 0) return;

    char cmd = command.charAt(0);

    switch (cmd) {
        case 'R': {
            // Red value
            int value = command.substring(1).toInt();
            if (value >= 0 && value <= 255) {
                current_r = (uint8_t)value;
                updateLED();
                Serial.print("Red set to: ");
                Serial.println(current_r);
            } else {
                Serial.println("Error: Red value must be 0-255");
            }
            break;
        }

        case 'G': {
            // Green value
            int value = command.substring(1).toInt();
            if (value >= 0 && value <= 255) {
                current_g = (uint8_t)value;
                updateLED();
                Serial.print("Green set to: ");
                Serial.println(current_g);
            } else {
                Serial.println("Error: Green value must be 0-255");
            }
            break;
        }

        case 'B': {
            // Blue value
            int value = command.substring(1).toInt();
            if (value >= 0 && value <= 255) {
                current_b = (uint8_t)value;
                updateLED();
                Serial.print("Blue set to: ");
                Serial.println(current_b);
            } else {
                Serial.println("Error: Blue value must be 0-255");
            }
            break;
        }

        case 'C': {
            // Set all RGB at once: C<R>,<G>,<B>
            String values = command.substring(1);
            int comma1 = values.indexOf(',');
            int comma2 = values.indexOf(',', comma1 + 1);

            if (comma1 > 0 && comma2 > comma1) {
                int r = values.substring(0, comma1).toInt();
                int g = values.substring(comma1 + 1, comma2).toInt();
                int b = values.substring(comma2 + 1).toInt();

                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                    setColor((uint8_t)r, (uint8_t)g, (uint8_t)b);
                } else {
                    Serial.println("Error: RGB values must be 0-255");
                }
            } else {
                Serial.println("Error: Format should be C<R>,<G>,<B> (e.g., C255,100,0)");
            }
            break;
        }

        case 'H': {
            // Help
            printHelp();
            break;
        }

        case '?': {
            // Show current color
            printCurrentColor();
            break;
        }

        default:
            Serial.print("Unknown command: ");
            Serial.println(command);
            Serial.println("Type 'H' for help");
            break;
    }
}

void printHelp() {
    Serial.println("--- Available Commands ---");
    Serial.println("R<0-255>       : Set Red value");
    Serial.println("G<0-255>       : Set Green value");
    Serial.println("B<0-255>       : Set Blue value");
    Serial.println("C<R>,<G>,<B>   : Set all RGB at once (e.g., C255,100,0)");
    Serial.println("H              : Show this help");
    Serial.println("?              : Show current color");
    Serial.println("--------------------------");
}

void printCurrentColor() {
    Serial.print("Current color: RGB(");
    Serial.print(current_r);
    Serial.print(", ");
    Serial.print(current_g);
    Serial.print(", ");
    Serial.print(current_b);
    Serial.print("), HUE: ");
    Serial.println(currentHue);
}
