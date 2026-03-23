/**
 * @file protocol.cpp
 * @brief Inter-MCU Communication Protocol Implementation
 * @date 2025-12-15
 */

#include "protocol.h"

// ============================================================================
// CRC8 Calculation
// ============================================================================
/**
 * @brief Calculate CRC8 checksum using polynomial 0x07
 * @param data Pointer to data buffer
 * @param len Data length in bytes
 * @return CRC8 checksum value
 *
 * Algorithm:
 * - Polynomial: 0x07 (x^8 + x^2 + x + 1)
 * - Initial value: 0x00
 * - MSB first processing
 *
 * Example:
 *   uint8_t data[] = {0xAA, 0x00, 0x00, 0xC8, 0x42, 0x00, 0x00, 0xB4, 0x42, 0x55};
 *   uint8_t crc = calcCRC8(data, 10);  // Calculate CRC for first 10 bytes
 */
uint8_t calcCRC8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;  // Initial value

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];  // XOR byte into CRC

        // Process 8 bits
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;  // MSB is 1: shift and XOR with polynomial
            } else {
                crc = (crc << 1);          // MSB is 0: just shift
            }
        }
    }

    return crc;
}

// ============================================================================
// Utility Functions
// ============================================================================
/**
 * @brief Validate MotorCmd frame integrity
 * @param cmd Reference to MotorCmd struct
 * @return true if valid (header, footer, CRC match), false otherwise
 */
bool validateMotorCmd(const MotorCmd& cmd) {
    // Check header
    if (cmd.header != MOTOR_CMD_HEADER) {
        return false;
    }

    // Check footer
    if (cmd.footer != MOTOR_CMD_FOOTER) {
        return false;
    }

    // Calculate CRC8 (exclude checksum field itself)
    uint8_t calculated_crc = calcCRC8((const uint8_t*)&cmd, MOTOR_CMD_SIZE - 1);

    // Verify checksum
    return (cmd.checksum == calculated_crc);
}

/**
 * @brief Create MotorCmd frame with automatic CRC calculation
 * @param pitch Pitch target angle (degrees, 0-300°)
 * @param yaw YAW target angle (degrees, 0-360°)
 * @return MotorCmd struct ready for transmission
 *
 * Example Usage:
 *   MotorCmd cmd = createMotorCmd(150.0, 180.0);
 *   Serial1.write((uint8_t*)&cmd, sizeof(cmd));
 */
MotorCmd createMotorCmd(float pitch, float yaw) {
    MotorCmd cmd;
    cmd.header = MOTOR_CMD_HEADER;
    cmd.pitch = pitch;
    cmd.yaw = yaw;
    cmd.footer = MOTOR_CMD_FOOTER;

    // Calculate CRC8 for entire frame except checksum field
    cmd.checksum = calcCRC8((const uint8_t*)&cmd, MOTOR_CMD_SIZE - 1);

    return cmd;
}
