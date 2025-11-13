/**
 * AS5600 Validation Test
 *
 * Purpose: Validate AS5600 magnetic encoder with side-mounted magnet configuration
 *
 * Hardware Setup:
 * - ESP32-S3 DevKit
 * - AS5600 Evaluation Board
 * - Standard disc magnet (6mm diameter × 2.5mm thickness)
 *
 * Wiring:
 * - SDA: GPIO8 (ESP32-S3)
 * - SCL: GPIO9 (ESP32-S3)
 * - VCC: 3.3V
 * - GND: GND
 *
 * Test Procedure:
 * 1. Mount disc magnet 90° rotated (side-facing sensor)
 * 2. Adjust distance: 0.5mm, 1mm, 2mm, 3mm
 * 3. Rotate magnet manually 360°
 * 4. Verify angle output 0-4095 (0-360°)
 * 5. Check magnet detection status
 * 6. Verify AGC value in stable range (128 ± 32)
 */

#include <Arduino.h>
#include <Wire.h>

// AS5600 I2C Address
#define AS5600_ADDR 0x36

// AS5600 Register Map
#define REG_RAW_ANGLE_H    0x0C  // Raw angle high byte
#define REG_RAW_ANGLE_L    0x0D  // Raw angle low byte
#define REG_ANGLE_H        0x0E  // Angle high byte (with start/end position)
#define REG_ANGLE_L        0x0F  // Angle low byte
#define REG_STATUS         0x0B  // Status register
#define REG_AGC            0x1A  // Automatic Gain Control
#define REG_MAGNITUDE_H    0x1B  // Magnitude high byte
#define REG_MAGNITUDE_L    0x1C  // Magnitude low byte

// Pin Configuration (ESP32-S3)
#define SDA_PIN 8
#define SCL_PIN 9

// Status Register Bits
#define STATUS_MH  0x08  // Magnet too strong
#define STATUS_ML  0x10  // Magnet too weak
#define STATUS_MD  0x20  // Magnet detected

// Function prototypes
bool checkConnection();
uint16_t readRawAngle();
uint8_t readStatus();
uint8_t readAGC();
uint16_t readMagnitude();
uint8_t readRegister8(uint8_t reg);
uint16_t readRegister16(uint8_t reg);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("AS5600 Validation Test");
  Serial.println("Side-Mounted Magnet Configuration");
  Serial.println("========================================");
  Serial.println();

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // 400kHz Fast Mode

  delay(100);

  // Check AS5600 connection
  if (checkConnection()) {
    Serial.println("✓ AS5600 detected");
  } else {
    Serial.println("✗ AS5600 NOT detected - Check wiring!");
    while(1) delay(1000);
  }

  Serial.println();
  Serial.println("Test started - Rotate magnet slowly 360°");
  Serial.println("----------------------------------------");
  Serial.println();
}

void loop() {
  // Read angle and status
  uint16_t rawAngle = readRawAngle();
  uint8_t status = readStatus();
  uint8_t agc = readAGC();
  uint16_t magnitude = readMagnitude();

  // Convert to degrees
  float degrees = rawAngle * 360.0 / 4096.0;

  // Status flags
  bool magnetDetected = status & STATUS_MD;
  bool magnetTooStrong = status & STATUS_MH;
  bool magnetTooWeak = status & STATUS_ML;

  // AGC range check (optimal: 128 ± 32)
  bool agcOptimal = (agc >= 96 && agc <= 160);

  // Print results
  Serial.print("Raw: ");
  Serial.print(rawAngle);
  Serial.print("\t");

  Serial.print("Angle: ");
  Serial.print(degrees, 2);
  Serial.print("°\t");

  Serial.print("Status: ");
  if (magnetDetected) {
    Serial.print("✓ DETECTED ");
  } else {
    Serial.print("✗ NO MAGNET ");
  }

  if (magnetTooStrong) {
    Serial.print("[TOO STRONG] ");
  }
  if (magnetTooWeak) {
    Serial.print("[TOO WEAK] ");
  }
  Serial.print("\t");

  Serial.print("AGC: ");
  Serial.print(agc);
  if (agcOptimal) {
    Serial.print(" ✓\t");
  } else {
    Serial.print(" ✗\t");
  }

  Serial.print("Magnitude: ");
  Serial.print(magnitude);
  Serial.println();

  delay(100);  // 10Hz update rate
}

/**
 * Check if AS5600 is connected
 */
bool checkConnection() {
  Wire.beginTransmission(AS5600_ADDR);
  return (Wire.endTransmission() == 0);
}

/**
 * Read raw angle (0-4095)
 */
uint16_t readRawAngle() {
  return readRegister16(REG_RAW_ANGLE_H);
}

/**
 * Read status register
 */
uint8_t readStatus() {
  return readRegister8(REG_STATUS);
}

/**
 * Read AGC value
 */
uint8_t readAGC() {
  return readRegister8(REG_AGC);
}

/**
 * Read magnitude (magnetic field strength)
 */
uint16_t readMagnitude() {
  return readRegister16(REG_MAGNITUDE_H);
}

/**
 * Read 8-bit register
 */
uint8_t readRegister8(uint8_t reg) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(AS5600_ADDR, 1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0;
}

/**
 * Read 16-bit register (big-endian)
 */
uint16_t readRegister16(uint8_t reg) {
  Wire.beginTransmission(AS5600_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(AS5600_ADDR, 2);
  if (Wire.available() >= 2) {
    uint8_t high = Wire.read();
    uint8_t low = Wire.read();
    return (high << 8) | low;
  }
  return 0;
}
