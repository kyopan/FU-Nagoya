#ifndef YAW_CONTROLLER_H
#define YAW_CONTROLLER_H

#include <Arduino.h>

class YawController {
public:
  YawController();

  // Configuration
  void setPID(float p, float i, float d);
  void setLimits(float max_velocity, float saturation_velocity);

  // Main update loop
  // current_heading: 0-360 degrees from BNO055
  // target_heading: 0-360 degrees target
  // current_motor_velocity: rad/s from AS5600
  // Returns: target motor velocity (rad/s)
  float update(float current_heading, float target_heading,
               float current_motor_velocity);

  // State query
  // State query
  bool isSaturated() const { return _is_saturated; }
  float getIntegral() const { return _integral; }
  float getTargetVelocity() const { return _target_wheel_velocity; }

private:
  float _target_wheel_velocity; // Integrator for acceleration -> velocity
                                // conversion

  // PID constants
  float _kp;
  float _ki;
  float _kd;

  // State variables
  float _integral;
  float _prev_error;
  unsigned long _last_msg_time;

  // Saturation management
  float _max_velocity;         // Absolute max limit
  float _saturation_threshold; // Threshold to trigger unwind
  bool _is_saturated;
  unsigned long _saturation_start_time;

  // Dither
  unsigned long _last_dither_time;
  bool _dither_sign;

  // Helper: Shortest path error calculation
  float calculateShortestPathError(float current, float target);
};

#endif
