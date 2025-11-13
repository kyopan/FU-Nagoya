/***********************************************************************
 *   ________  ______  ______   __  _____
 *  /_  __/ / / / __ )/ ____/  / / / /__ \
 *   / / / / / / __  / __/    / / / /__/ /
 *  / / / /_/ / /_/ / /___   / /_/ // __/
 * /_/  \____/_____/_____/   \____//____/
 *
 * FU Tube v2 - Pitch Motor Test
 * BLDC Motor + Variable Resistor (50kΩ)
 *
 * Author: Kyopalab. LLC
 * Creation Date: 2025/11/13
 * Version: 0.1
 * License: Proprietary License
 * MCU: ESP32-S3-MINI-1-N8
 ************************************************************************/

#include <Arduino.h>
#include <SimpleFOC.h>

// ========================================
// Pin Definitions (ESP32-S3 #2)
// ========================================

// Variable Resistor (Pitch Angle Sensor)
#define VR_PIN              GPIO_NUM_10  // ADC input for angle sensing

// BLDC Motor PWM Pins (Pitch Motor)
#define PIN_PWM_U           GPIO_NUM_14  // U-phase PWM
#define PIN_PWM_V           GPIO_NUM_15  // V-phase PWM
#define PIN_PWM_W           GPIO_NUM_16  // W-phase PWM

// Serial Communication
#define BAUD_USB            115200

// ========================================
// Variable Resistor Configuration
// ========================================
#define VR_ADC_MAX          4095         // 12-bit ADC resolution
#define VR_ANGLE_MAX        300.0f       // Maximum rotation angle (degrees)
#define VR_VOLTAGE_MAX      3.3f         // ADC reference voltage

// Calibration values (will be updated during calibration)
uint16_t g_adc_min = 0;
uint16_t g_adc_max = VR_ADC_MAX;
bool g_calibration_done = false;

// ========================================
// BLDC Motor Configuration
// ========================================
// Motor Specification (from v1 firmware)
// - Pole pairs: 7
// - Phase resistance: 6.5Ω
// - KV rating: 330

BLDCMotor motor = BLDCMotor(7, 6.5, 330);  // (pole_pairs, phase_resistance, KV_rating)
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_PWM_U, PIN_PWM_V, PIN_PWM_W);

// ========================================
// Custom Variable Resistor Sensor Class
// ========================================
class VariableResistorSensor : public Sensor {
public:
    VariableResistorSensor() {}

    void init() override {
        // ADC initialization
        analogReadResolution(12);  // Set 12-bit resolution
        pinMode(VR_PIN, INPUT);
        Serial.println("Variable Resistor Sensor initialized");
    }

    // Read angle in radians
    float getAngle() override {
        uint16_t adc_value = analogRead(VR_PIN);

        // Apply calibration
        if (g_calibration_done) {
            adc_value = constrain(adc_value, g_adc_min, g_adc_max);
            float angle_deg = map(adc_value, g_adc_min, g_adc_max, 0, (int)VR_ANGLE_MAX);
            return angle_deg * DEG_TO_RAD;  // Convert to radians
        } else {
            // Without calibration, use raw mapping
            float angle_deg = (adc_value / (float)VR_ADC_MAX) * VR_ANGLE_MAX;
            return angle_deg * DEG_TO_RAD;  // Convert to radians
        }
    }

    // Read raw ADC value (for debugging)
    uint16_t getADCValue() {
        return analogRead(VR_PIN);
    }

    // Get voltage output
    float getVoltage() {
        uint16_t adc_value = analogRead(VR_PIN);
        return (adc_value / (float)VR_ADC_MAX) * VR_VOLTAGE_MAX;
    }

    void update() override {
        // No update needed for ADC-based sensor
    }

    int needsSearch() override {
        return 0;  // No zero search needed
    }

    // Required by SimpleFOC Sensor base class
    float getSensorAngle() override {
        return getAngle();  // Delegate to getAngle()
    }
};

// Create sensor instance
VariableResistorSensor vr_sensor;

// ========================================
// Control Variables
// ========================================
float g_target_angle_deg = 0.0f;  // Target angle in degrees
bool g_motor_enabled = false;     // Motor enable flag

// ========================================
// Function Declarations
// ========================================
void performCalibration();
void showStatus();

// ========================================
// Setup Function
// ========================================
void setup() {
    // Serial initialization
    Serial.begin(BAUD_USB);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("FU Tube v2 - Pitch Motor Test");
    Serial.println("ESP32-S3 + BLDC + Variable Resistor");
    Serial.println("========================================\n");

    // Variable Resistor Sensor initialization
    vr_sensor.init();

    // BLDC Driver initialization
    driver.voltage_power_supply = 5.0;  // 5V power supply
    driver.init();
    Serial.println("BLDC Driver initialized");

    // Link sensor to motor
    motor.linkSensor(&vr_sensor);

    // Link driver to motor
    motor.linkDriver(&driver);

    // Motor control configuration (velocity mode with position feedback)
    motor.controller = MotionControlType::velocity;

    // Velocity PID parameters (from v1 firmware - optimized for smooth operation)
    motor.voltage_limit = 1.5;               // Voltage limit
    motor.PID_velocity.P = 0.1;              // Velocity P gain
    motor.PID_velocity.I = 0.3;              // Velocity I gain
    motor.PID_velocity.D = 0.01;             // Velocity D gain
    motor.LPF_velocity.Tf = 0.15;            // Low-pass filter time constant
    motor.velocity_limit = 1.3;              // Velocity limit (rad/s)
    motor.PID_velocity.output_ramp = 150.0;  // Output ramp rate

    // Motor initialization
    motor.init();
    motor.initFOC();

    Serial.println("Motor initialized with SimpleFOC");
    Serial.println("\n========================================");
    Serial.println("Commands:");
    Serial.println("  C        - Calibrate variable resistor");
    Serial.println("  E        - Enable motor");
    Serial.println("  D        - Disable motor");
    Serial.println("  T<angle> - Set target angle (e.g., T45)");
    Serial.println("  S        - Show current status");
    Serial.println("========================================\n");
}

// ========================================
// Loop Function
// ========================================
void loop() {
    // Motor FOC loop (must be called frequently)
    motor.loopFOC();

    // Position feedback control
    if (g_motor_enabled) {
        float current_angle = vr_sensor.getAngle();
        float target_angle_rad = g_target_angle_deg * DEG_TO_RAD;

        // Position error
        float position_error = target_angle_rad - current_angle;

        // P control for velocity command
        const float VELOCITY_P_GAIN = 2.5f;
        float target_velocity = position_error * VELOCITY_P_GAIN;

        // Velocity limit
        const float MAX_VELOCITY = 1.2f;
        target_velocity = constrain(target_velocity, -MAX_VELOCITY, MAX_VELOCITY);

        // Deadband
        const float POSITION_DEADBAND = 0.015f;  // ~0.86 degrees
        if (abs(position_error) < POSITION_DEADBAND) {
            target_velocity = 0.0f;
        }

        // Apply velocity command
        motor.move(target_velocity);
    } else {
        motor.move(0.0f);  // Stop motor
    }

    // Handle serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("C")) {
            // Calibration
            performCalibration();
        } else if (cmd.startsWith("E")) {
            // Enable motor
            g_motor_enabled = true;
            Serial.println("Motor ENABLED");
        } else if (cmd.startsWith("D")) {
            // Disable motor
            g_motor_enabled = false;
            Serial.println("Motor DISABLED");
        } else if (cmd.startsWith("T")) {
            // Set target angle
            float angle = cmd.substring(1).toFloat();
            g_target_angle_deg = constrain(angle, 0.0f, VR_ANGLE_MAX);
            Serial.printf("Target angle set to: %.2f degrees\n", g_target_angle_deg);
        } else if (cmd.startsWith("S")) {
            // Show status
            showStatus();
        } else {
            Serial.println("Unknown command");
        }
    }

    // Periodic status display (every 500ms)
    static unsigned long last_status_time = 0;
    if (millis() - last_status_time >= 500) {
        last_status_time = millis();

        // Compact status display
        float current_angle = vr_sensor.getAngle() * RAD_TO_DEG;
        uint16_t adc_value = vr_sensor.getADCValue();
        float voltage = vr_sensor.getVoltage();

        Serial.printf("ADC:%4d | Volt:%.2fV | Angle:%6.2f° | Target:%6.2f° | Motor:%s\n",
                      adc_value, voltage, current_angle, g_target_angle_deg,
                      g_motor_enabled ? "ON " : "OFF");
    }
}

// ========================================
// Calibration Function
// ========================================
void performCalibration() {
    Serial.println("\n========================================");
    Serial.println("Variable Resistor Calibration");
    Serial.println("========================================");
    Serial.println("Rotate the Pitch axis to 0° position");
    Serial.println("Press ENTER to continue...");

    // Wait for user input
    while (!Serial.available()) {
        delay(10);
    }
    Serial.readStringUntil('\n');  // Clear input

    // Read minimum ADC value (0° position)
    g_adc_min = vr_sensor.getADCValue();
    Serial.printf("0° position ADC value: %d\n", g_adc_min);

    Serial.println("\nRotate the Pitch axis to 300° position");
    Serial.println("Press ENTER to continue...");

    // Wait for user input
    while (!Serial.available()) {
        delay(10);
    }
    Serial.readStringUntil('\n');  // Clear input

    // Read maximum ADC value (300° position)
    g_adc_max = vr_sensor.getADCValue();
    Serial.printf("300° position ADC value: %d\n", g_adc_max);

    // Validate calibration
    if (g_adc_max > g_adc_min + 100) {
        g_calibration_done = true;
        Serial.println("\n✓ Calibration completed successfully!");
        Serial.printf("ADC range: %d - %d\n", g_adc_min, g_adc_max);
        Serial.printf("Resolution: %.3f°/LSB\n", VR_ANGLE_MAX / (g_adc_max - g_adc_min));
    } else {
        Serial.println("\n✗ Calibration failed - ADC range too small");
        g_calibration_done = false;
    }
    Serial.println("========================================\n");
}

// ========================================
// Status Display Function
// ========================================
void showStatus() {
    Serial.println("\n========================================");
    Serial.println("Current Status");
    Serial.println("========================================");

    // Variable Resistor Status
    uint16_t adc_value = vr_sensor.getADCValue();
    float voltage = vr_sensor.getVoltage();
    float angle_deg = vr_sensor.getAngle() * RAD_TO_DEG;

    Serial.println("Variable Resistor:");
    Serial.printf("  ADC Value: %d / %d\n", adc_value, VR_ADC_MAX);
    Serial.printf("  Voltage:   %.3f V\n", voltage);
    Serial.printf("  Angle:     %.2f degrees\n", angle_deg);

    // Calibration Status
    Serial.println("\nCalibration:");
    if (g_calibration_done) {
        Serial.printf("  Status:    DONE\n");
        Serial.printf("  ADC Min:   %d (0°)\n", g_adc_min);
        Serial.printf("  ADC Max:   %d (300°)\n", g_adc_max);
        Serial.printf("  Range:     %d\n", g_adc_max - g_adc_min);
        Serial.printf("  Resolution: %.3f°/LSB\n", VR_ANGLE_MAX / (g_adc_max - g_adc_min));
    } else {
        Serial.println("  Status:    NOT CALIBRATED");
    }

    // Motor Status
    Serial.println("\nMotor:");
    Serial.printf("  State:     %s\n", g_motor_enabled ? "ENABLED" : "DISABLED");
    Serial.printf("  Target:    %.2f degrees\n", g_target_angle_deg);
    Serial.printf("  Current:   %.2f degrees\n", angle_deg);
    Serial.printf("  Error:     %.2f degrees\n", g_target_angle_deg - angle_deg);

    // Motor Parameters
    Serial.println("\nMotor Parameters:");
    Serial.printf("  Voltage Limit:   %.2f V\n", motor.voltage_limit);
    Serial.printf("  Velocity Limit:  %.2f rad/s\n", motor.velocity_limit);
    Serial.printf("  P Gain:          %.3f\n", motor.PID_velocity.P);
    Serial.printf("  I Gain:          %.3f\n", motor.PID_velocity.I);
    Serial.printf("  D Gain:          %.3f\n", motor.PID_velocity.D);

    Serial.println("========================================\n");
}
