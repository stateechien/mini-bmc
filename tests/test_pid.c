/**
 * test_pid.c - PID Controller Unit Tests
 *
 * Build: gcc -o test_pid test_pid.c ../firmware/src/pid_control.c
 *        -I../firmware/include -lm
 * Run:   ./test_pid
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "pid_control.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_NEAR(actual, expected, tolerance, msg) do { \
    double _a = (actual), _e = (expected), _t = (tolerance); \
    if (fabs(_a - _e) <= _t) { \
        tests_passed++; \
        printf("  ✓ %s (%.4f ≈ %.4f)\n", msg, _a, _e); \
    } else { \
        tests_failed++; \
        printf("  ✗ %s (%.4f != %.4f ± %.4f)\n", msg, _a, _e, _t); \
    } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (cond) { \
        tests_passed++; \
        printf("  ✓ %s\n", msg); \
    } else { \
        tests_failed++; \
        printf("  ✗ %s (FAILED)\n", msg); \
    } \
} while(0)

/* ── Test Cases ── */

void test_pid_initialization(void)
{
    printf("\n[TEST] PID Initialization\n");

    pid_state_t pid;
    pid_init(&pid, 2.0, 0.1, 0.5, 65.0);

    ASSERT_NEAR(pid.kp, 2.0, 0.001, "Kp initialized correctly");
    ASSERT_NEAR(pid.ki, 0.1, 0.001, "Ki initialized correctly");
    ASSERT_NEAR(pid.kd, 0.5, 0.001, "Kd initialized correctly");
    ASSERT_NEAR(pid.setpoint, 65.0, 0.001, "Setpoint initialized");
    ASSERT_NEAR(pid.integral, 0.0, 0.001, "Integral starts at 0");
    ASSERT_NEAR(pid.prev_error, 0.0, 0.001, "Prev error starts at 0");
}

void test_pid_at_setpoint(void)
{
    printf("\n[TEST] PID at Setpoint (no correction needed)\n");

    pid_state_t pid;
    pid_init(&pid, 2.0, 0.1, 0.5, 65.0);

    /* When temperature = setpoint, error = 0 */
    double output = pid_compute(&pid, 65.0, 1.0);

    /* Output should be near the base duty (40%) */
    ASSERT_NEAR(output, 40.0, 5.0, "Output near base duty at setpoint");
}

void test_pid_above_setpoint(void)
{
    printf("\n[TEST] PID Above Setpoint (needs cooling)\n");

    pid_state_t pid;
    pid_init(&pid, 2.0, 0.1, 0.5, 65.0);

    /* Temperature 10°C above setpoint */
    double output = pid_compute(&pid, 75.0, 1.0);

    /* Should increase fan duty significantly */
    ASSERT_TRUE(output > 50.0, "Fan duty increases when too hot");
    ASSERT_TRUE(output <= 100.0, "Fan duty clamped to max 100%");
}

void test_pid_below_setpoint(void)
{
    printf("\n[TEST] PID Below Setpoint (can reduce cooling)\n");

    pid_state_t pid;
    pid_init(&pid, 2.0, 0.1, 0.5, 65.0);

    /* Temperature well below setpoint */
    double output = pid_compute(&pid, 40.0, 1.0);

    /* Should reduce fan duty */
    ASSERT_TRUE(output < 40.0, "Fan duty decreases when cold");
    ASSERT_TRUE(output >= 10.0, "Fan duty respects minimum limit");
}

void test_pid_output_clamping(void)
{
    printf("\n[TEST] PID Output Clamping\n");

    pid_state_t pid;
    pid_init(&pid, 10.0, 1.0, 0.0, 65.0);
    pid_set_output_limits(&pid, 20.0, 80.0);

    /* Very high temperature → should clamp at max */
    double output = pid_compute(&pid, 100.0, 1.0);
    ASSERT_TRUE(output <= 80.0, "Output clamped to upper limit (80%)");

    /* Very low temperature → should clamp at min */
    pid_reset(&pid);
    output = pid_compute(&pid, 10.0, 1.0);
    ASSERT_TRUE(output >= 20.0, "Output clamped to lower limit (20%)");
}

void test_pid_convergence(void)
{
    printf("\n[TEST] PID Convergence (steady state)\n");

    pid_state_t pid;
    pid_init(&pid, 3.0, 0.1, 1.5, 65.0);

    /* Simulate 100 iterations at constant temperature */
    double temp = 70.0;  /* Start above setpoint */
    double output;

    for (int i = 0; i < 100; i++) {
        output = pid_compute(&pid, temp, 1.0);
        /* Simple thermal model: fan cools proportionally */
        temp += (55.0 + 15.0 - (output / 100.0) * 25.0 - temp) * 0.1;
    }

    ASSERT_NEAR(temp, 65.0, 5.0,
        "Temperature converges near setpoint after 100 iterations");
    printf("  → Final temp=%.2f°C, duty=%.1f%%\n", temp, output);
}

void test_pid_reset(void)
{
    printf("\n[TEST] PID Reset\n");

    pid_state_t pid;
    pid_init(&pid, 2.0, 0.1, 0.5, 65.0);

    /* Accumulate some state */
    pid_compute(&pid, 80.0, 1.0);
    pid_compute(&pid, 80.0, 1.0);

    ASSERT_TRUE(pid.integral != 0.0, "Integral accumulated before reset");

    pid_reset(&pid);
    ASSERT_NEAR(pid.integral, 0.0, 0.001, "Integral zeroed after reset");
    ASSERT_NEAR(pid.prev_error, 0.0, 0.001, "Prev error zeroed after reset");
}

/* ── Main ── */

int main(void)
{
    printf("╔══════════════════════════════════════╗\n");
    printf("║    PID Controller Unit Tests         ║\n");
    printf("╚══════════════════════════════════════╝\n");

    test_pid_initialization();
    test_pid_at_setpoint();
    test_pid_above_setpoint();
    test_pid_below_setpoint();
    test_pid_output_clamping();
    test_pid_convergence();
    test_pid_reset();

    printf("\n══════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n",
           tests_passed, tests_failed);
    printf("══════════════════════════════════════\n");

    return tests_failed > 0 ? 1 : 0;
}
