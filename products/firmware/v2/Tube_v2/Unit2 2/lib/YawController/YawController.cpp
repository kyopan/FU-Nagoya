#include "YawController.h"

YawController::YawController() {
  _kp = 0.0f;
  _ki = 0.0f;
  _kd = 0.0f;
  _integral = 0.0f;
  _prev_error = 0.0f;
  _last_msg_time = 0;

  _max_velocity = 400.0f;         // Default high limit
  _saturation_threshold = 150.0f; // Lower threshold (was 200)
  _is_saturated = false;

  _last_dither_time = 0;
  _dither_sign = false;

  _target_wheel_velocity = 0.0f;
}

void YawController::setPID(float p, float i, float d) {
  _kp = p;
  _ki = i;
  _kd = d;
}

void YawController::setLimits(float max_velocity, float saturation_velocity) {
  _max_velocity = max_velocity;
  _saturation_threshold = saturation_velocity;
}

float YawController::calculateShortestPathError(float current, float target) {
  float error = target - current;
  while (error <= -180.0f)
    error += 360.0f;
  while (error > 180.0f)
    error -= 360.0f;

  return error;
}

float YawController::update(float current_heading, float target_heading,
                            float current_motor_velocity) {
  unsigned long now = millis();
  float dt = (now - _last_msg_time) / 1000.0f;
  if (dt <= 0.0f || dt > 1.0f)
    dt = 0.02f; // Safety clamp or first run
  _last_msg_time = now;

  // 1. Saturation Check
  // If we are spinning too fast, we lose authority or risk damage.
  if (abs(current_motor_velocity) > _saturation_threshold) {
    if (!_is_saturated) {
      _is_saturated = true;
      _saturation_start_time = now;
    }
  }

  // 2. Unwind Logic (If saturated)
  if (_is_saturated) {
    // Hysteresis: Exit saturation only when speed drops significantly
    if (abs(current_motor_velocity) < (_saturation_threshold * 0.8f)) {
      _is_saturated = false;
      _integral = 0.0f; // Reset I-term on exit
      // Smooth handoff: Set internal integrator to current actual velocity
      _target_wheel_velocity = current_motor_velocity;
    } else {
      // Decelerate: Set target velocity towards 0
      // We decay the target velocity to induce braking torque
      if (_target_wheel_velocity > 0) {
        _target_wheel_velocity -=
            500.0f * dt; // Stronger Decel rate: 500 rad/s^2
        if (_target_wheel_velocity < 0)
          _target_wheel_velocity = 0;
      } else {
        _target_wheel_velocity += 500.0f * dt;
        if (_target_wheel_velocity > 0)
          _target_wheel_velocity = 0;
      }
      return _target_wheel_velocity;
    }
  }

  // 3. Normal Control Logic (Reaction Wheel Physics)
  // Position Error -> PID Output = REQUIRED TORQUE (aka Acceleration)
  // We integrate Acceleration to get Target Velocity.

  float error = calculateShortestPathError(current_heading, target_heading);

  // ★ ERROR CLAMPING (v2.2.68)
  // Limited to 45 deg (was 20) to allow stronger recovery.
  float clamped_error = error;
  if (clamped_error > 45.0f)
    clamped_error = 45.0f;
  if (clamped_error < -45.0f)
    clamped_error = -45.0f;

  // I-term: Accumulate error to overcome cable torsion
  // v2.2.68: Removed 30-deg reset logic. Always accumulate (with Clamp).
  // Anti-Windup Clamp: +/- 1000.0f
  _integral += clamped_error * dt;
  if (_integral > 1000.0f)
    _integral = 1000.0f;
  if (_integral < -1000.0f)
    _integral = -1000.0f;

  // D-term
  float derivative = (clamped_error - _prev_error) / dt;
  _prev_error = clamped_error;

  // PID Output is now ACCELERATION (rad/s^2)
  float safe_kp = _kp * 2.0f;
  // ★ v2.2.70 Gain Scheduling: Boost Mode
  // If error > 10deg, apply 2x Boost to Kick-Start the movement.
  // Otherwise, use Normal P for smooth arrival/holding.
  if (abs(clamped_error) > 10.0f) {
    safe_kp *= 2.0f;
  }

  float output_accel =
      (safe_kp * clamped_error) + (_ki * _integral) + (_kd * derivative);

  // ★ REACTION WHEEL INVERSION
  // To rotate the BODY right (positive), we must accelerate the WHEEL left
  // (negative). Action-Reaction Law.
  output_accel *= -1.0f;

  // 4. Integrate Acceleration to get Velocity
  // v_target = v_prev + a * dt
  _target_wheel_velocity += output_accel * dt;

  // ★ Leaky Integrator / Mechanical Friction Simulation
  // Prevent infinite spin-up by applying a decay factor.
  // v2.2.35: Increased decay to 0.980 (2.0% loss/tick)
  // v2.2.66: Increased decay to 0.900 (10% loss/tick) to prevent windup against
  // cable.  // v2.2.67: Relaxed decay to 0.960 (4% loss/tick) to fix Drift
  // (allow holding torque). v2.2.71: Increased decay to 0.950 (5% loss/tick) to
  // dampen oscillation energy. Stronger decay to enforce unwind vs I-term
  // buildup.
  _target_wheel_velocity *= 0.950f;

  // 5. Anti-Stiction (Dither) - DISABLED (v2.2.36)
  // Causing BNO055 drift due to vibration.
  /*
  if (now - _last_dither_time > 20) {
    _last_dither_time = now;
    _dither_sign = !_dither_sign;
  }
  */

  float final_velocity_cmd = _target_wheel_velocity;

  /*
  if (_dither_sign) {
    final_velocity_cmd += 2.0f;
  } else {
    final_velocity_cmd -= 2.0f;
  }
  */

  // 6. Output Clamping (Velocity Limit)
  if (final_velocity_cmd > _max_velocity) {
    final_velocity_cmd = _max_velocity;
    // Anti-windup for the velocity integrator
    if (_target_wheel_velocity > _max_velocity)
      _target_wheel_velocity = _max_velocity;
  }
  if (final_velocity_cmd < -_max_velocity) {
    final_velocity_cmd = -_max_velocity;
    if (_target_wheel_velocity < -_max_velocity)
      _target_wheel_velocity = -_max_velocity;
  }

  return final_velocity_cmd;
}
