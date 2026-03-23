/**
 * @file protocol.h
 * @brief Inter-MCU Communication Protocol (Binary Frame + CRC8)
 * @date 2025-12-15
 *
 * ESP32S3 #1 → ESP32S3 #2: Motor control commands (Pitch/YAW angles)
 * UART: 115200 bps
 * Frame Size: 11 bytes
 * Transfer Time: 0.69ms @ 115200 bps
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

// ============================================================================
// Binary Frame Structure (11 bytes)
// ============================================================================
/**
 * @struct MotorCmd
 * @brief Binary command frame for motor control
 *
 * Frame Layout:
 * [0]     : Header (0xAA)
 * [1-4]   : Pitch target angle (float, 4 bytes)
 * [5-8]   : YAW target angle (float, 4 bytes)
 * [9]     : Footer (0x55)
 * [10]    : CRC8 Checksum
 */
struct MotorCmd {
    uint8_t header = 0xAA;     // Frame start marker
    float pitch;               // Pitch target angle (degrees, 0-300°)
    float yaw;                 // YAW target angle (degrees, 0-360°)
    uint8_t footer = 0x55;     // Frame end marker
    uint8_t checksum;          // CRC8 checksum (polynomial 0x07)
} __attribute__((packed));

// ============================================================================
// CRC8 Calculation
// ============================================================================
/**
 * @brief Calculate CRC8 checksum
 * @param data Pointer to data buffer
 * @param len Data length in bytes
 * @return CRC8 checksum value
 *
 * Polynomial: 0x07 (x^8 + x^2 + x + 1)
 * Initial value: 0x00
 */
uint8_t calcCRC8(const uint8_t* data, size_t len);

// ============================================================================
// Utility Functions
// ============================================================================
/**
 * @brief Validate MotorCmd frame integrity
 * @param cmd Reference to MotorCmd struct
 * @return true if valid (header, footer, CRC match), false otherwise
 */
bool validateMotorCmd(const MotorCmd& cmd);

/**
 * @brief Create MotorCmd frame with automatic CRC calculation
 * @param pitch Pitch target angle (degrees)
 * @param yaw YAW target angle (degrees)
 * @return MotorCmd struct ready for transmission
 */
MotorCmd createMotorCmd(float pitch, float yaw);

// ============================================================================
// Constants
// ============================================================================
constexpr uint8_t MOTOR_CMD_HEADER = 0xAA;
constexpr uint8_t MOTOR_CMD_FOOTER = 0x55;
constexpr size_t MOTOR_CMD_SIZE = sizeof(MotorCmd);
constexpr uint32_t UART_BAUD_RATE = 115200;

#endif // PROTOCOL_H
