/**
 * pid_control.h - PID-Based Thermal Controller
 *
 * 【學習重點】
 * - BMC 的重要功能之一：根據溫度調整風扇轉速
 * - 開迴路 (Open-loop): 查表 (溫度→風扇速度)，簡單但不精確
 * - 閉迴路 (Closed-loop): PID 控制，持續修正，這就是你 TSN 論文做的！
 *
 * PID 公式：
 *   output = Kp * error + Ki * ∫error·dt + Kd * d(error)/dt
 *
 * 在 BMC 情境中：
 *   - Process Variable (PV) = CPU 溫度
 *   - Setpoint (SP) = 目標溫度 (e.g., 65°C)
 *   - Control Variable (CV) = 風扇 duty cycle (0~100%)
 *   - error = SP - PV (溫度太高→error 為負→加大風扇)
 *
 * 跟你 TSN 研究的對比：
 *   TSN: PV = 延遲, SP = 目標延遲, CV = GCL time slot
 *   BMC: PV = 溫度, SP = 目標溫度, CV = 風扇轉速
 *   同樣的數學，不同的物理量！
 */

#ifndef PID_CONTROL_H
#define PID_CONTROL_H

#include "bmc_state.h"

/**
 * Initialize PID controller with tuned parameters.
 *
 * @param pid       PID state structure
 * @param kp        Proportional gain
 * @param ki        Integral gain
 * @param kd        Derivative gain
 * @param setpoint  Target temperature in °C
 */
void pid_init(pid_state_t *pid, double kp, double ki, double kd,
              double setpoint);

/**
 * Compute one PID iteration.
 *
 * @param pid              PID state (updated in place)
 * @param current_temp     Current measured temperature (°C)
 * @param dt               Time step in seconds
 * @return                 Fan duty cycle output (0.0 ~ 100.0)
 */
double pid_compute(pid_state_t *pid, double current_temp, double dt);

/**
 * Reset PID integral and derivative state.
 * Call when switching from manual to auto mode.
 */
void pid_reset(pid_state_t *pid);

/**
 * Anti-windup: clamp integral term to prevent overshoot.
 */
void pid_set_output_limits(pid_state_t *pid, double min, double max);

#endif /* PID_CONTROL_H */
