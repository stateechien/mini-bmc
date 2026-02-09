/**
 * pid_control.c - PID Closed-Loop Thermal Controller
 *
 * 論文中做的 PID：
 *   error = target_delay - measured_delay
 *   output = 調整 GCL time slot
 *
 * BMC 的 PID 完全相同的數學：
 *   error = target_temp - measured_temp
 *   output = 調整 fan duty cycle
 *
 * Anti-windup 很重要：
 *   如果 fan 已經 100% 但溫度還是高，integral 會一直累積
 *   → 溫度降下來後 fan 遲遲不降速 (windup 問題)
 *   解法：clamp integral term，跟論文試過的實驗 Stage 6/7 做的一樣
 *
 */

#include <stdio.h>
#include <math.h>

#include "pid_control.h"

void pid_init(pid_state_t *pid, double kp, double ki, double kd,
              double setpoint)
{
    pid->kp         = kp;
    pid->ki         = ki;
    pid->kd         = kd;
    pid->setpoint   = setpoint;
    pid->integral   = 0.0;
    pid->prev_error = 0.0;
    pid->output     = 30.0;    /* Start at 30% duty */
    pid->output_min = 10.0;    /* Never go below 10% (safety) */
    pid->output_max = 100.0;

    printf("[PID] Initialized: Kp=%.2f Ki=%.3f Kd=%.2f SP=%.1f°C\n",
           kp, ki, kd, setpoint);
}

double pid_compute(pid_state_t *pid, double current_temp, double dt)
{
    if (dt <= 0.0) dt = 1.0;

    /*
     * Error convention: positive error = too hot → need more cooling
     * (Note: this is REVERSED from typical PID because higher output
     *  = more cooling = lower temperature)
     */
    double error = current_temp - pid->setpoint;

    /* ── Proportional term ── */
    double p_term = pid->kp * error;

    /* ── Integral term with anti-windup (clamping method) ── */
    pid->integral += error * dt;

    /* Anti-windup: clamp integral to prevent excessive accumulation */
    double integral_limit = (pid->output_max - pid->output_min) / pid->ki;
    if (pid->ki > 0) {
        if (pid->integral >  integral_limit) pid->integral =  integral_limit;
        if (pid->integral < -integral_limit) pid->integral = -integral_limit;
    }

    double i_term = pid->ki * pid->integral;

    /* ── Derivative term (on error, with low-pass filtering) ── */
    double derivative = (error - pid->prev_error) / dt;
    double d_term = pid->kd * derivative;
    pid->prev_error = error;

    /* ── Compute output ── */
    double output = p_term + i_term + d_term;

    /* Offset: base duty cycle when error = 0 */
    output += 40.0;  /* 40% base duty */

    /* Output clamping */
    if (output < pid->output_min) output = pid->output_min;
    if (output > pid->output_max) output = pid->output_max;

    pid->output = output;
    return output;
}

void pid_reset(pid_state_t *pid)
{
    pid->integral   = 0.0;
    pid->prev_error = 0.0;
    printf("[PID] Controller reset\n");
}

void pid_set_output_limits(pid_state_t *pid, double min, double max)
{
    if (min >= max) return;
    pid->output_min = min;
    pid->output_max = max;

    /* Re-clamp current output */
    if (pid->output < min) pid->output = min;
    if (pid->output > max) pid->output = max;

    printf("[PID] Output limits set: [%.1f%%, %.1f%%]\n", min, max);
}
