/**
 * main.c - BMC Firmware Daemon Entry Point
 *
 * 【學習重點 - BMC Daemon 架構】
 *
 * 真實 BMC 韌體的啟動流程 (以 OpenBMC 為例):
 * 1. U-Boot bootloader 啟動
 * 2. Linux kernel 啟動 (BMC 跑的是 embedded Linux)
 * 3. systemd 啟動各種 service:
 *    - phosphor-hwmon (sensor monitoring)
 *    - phosphor-pid-control (thermal management)
 *    - bmcweb (Redfish API server)
 *    - phosphor-host-ipmid (IPMI over KCS)
 *    - phosphor-logging (event log)
 *
 * 這個 daemon 合併了上述功能到單一 process，
 * 用多執行緒模擬各 service 的角色。
 *
 * 主迴圈:
 *   1. 讀取 sensor (模擬 hwmon)
 *   2. PID 計算新的 fan duty (模擬 pid-control)
 *   3. 更新狀態到 JSON (模擬 D-Bus state export)
 *   4. 背景: IPMI listener thread 處理外部命令
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "bmc_state.h"
#include "sensor.h"
#include "ipmi.h"
#include "pid_control.h"
#include "event_log.h"
#include "secure_boot.h"

/* Global state */
static bmc_state_t g_state;

/* Signal handler for graceful shutdown */
static void signal_handler(int sig)
{
    printf("\n[MAIN] Received signal %d, shutting down...\n", sig);
    g_state.running = false;
}

/* ── Banner ── */

static void print_banner(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════╗\n");
    printf("║         Mini BMC Simulator v1.0          ║\n");
    printf("║    Baseboard Management Controller       ║\n");
    printf("║         Firmware Daemon                  ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("\n");
}

/* ── Find CPU temperature sensor index ── */
static int find_cpu_temp_sensor(bmc_state_t *state)
{
    for (int i = 0; i < state->sensor_count; i++) {
        if (strcmp(state->sensors[i].name, "CPU_Temp") == 0)
            return i;
    }
    return 0;  /* fallback to first sensor */
}

/* ── Main ── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    print_banner();

    /* Setup signal handlers */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* ── Phase 1: Initialize all subsystems ── */
    printf("[MAIN] Phase 1: Initializing subsystems...\n\n");

    if (bmc_state_init(&g_state) != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize BMC state\n");
        return 1;
    }

    if (sel_init(&g_state) != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize event log\n");
        return 1;
    }

    sel_add_entry(&g_state, SEL_SEVERITY_INFO, "System",
                  "BMC daemon starting up");

    if (sensor_init(&g_state) != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize sensors\n");
        return 1;
    }

    /* Initialize PID thermal controller
     *
     * Tuning rationale:
     *   Kp = 3.0  : Moderate proportional response
     *   Ki = 0.1  : Slow integral to avoid overshoot
     *   Kd = 1.5  : Moderate derivative for damping
     *   SP = 65°C : Target CPU temperature
     *
     * These are similar to values used in real OpenBMC
     * phosphor-pid-control configurations.
     */
    pid_init(&g_state.pid, 3.0, 0.1, 1.5, 65.0);
    pid_set_output_limits(&g_state.pid, 10.0, 100.0);

    if (secure_boot_init(&g_state) != 0) {
        fprintf(stderr, "[MAIN] Failed to initialize secure boot\n");
        return 1;
    }

    /* ── Phase 2: Secure Boot Verification ── */
    printf("\n[MAIN] Phase 2: Running secure boot verification...\n\n");
    bool boot_ok = secure_boot_verify(&g_state);
    if (!boot_ok) {
        sel_add_entry(&g_state, SEL_SEVERITY_CRITICAL, "System",
            "Secure boot verification FAILED - continuing in degraded mode");
        printf("[MAIN] WARNING: Secure boot failed! "
               "Continuing in degraded mode.\n\n");
    } else {
        sel_add_entry(&g_state, SEL_SEVERITY_INFO, "System",
            "Secure boot verification passed");
    }

    /* ── Phase 3: Start IPMI listener ── */
    printf("\n[MAIN] Phase 3: Starting IPMI listener...\n\n");
    if (ipmi_start_listener(&g_state) != 0) {
        fprintf(stderr, "[MAIN] Failed to start IPMI listener\n");
        /* Non-fatal: continue without IPMI */
    }

    /* ── Phase 4: Main sensor polling + PID control loop ── */
    printf("[MAIN] Phase 4: Entering main control loop "
           "(Ctrl+C to stop)\n\n");

    sel_add_entry(&g_state, SEL_SEVERITY_INFO, "System",
                  "BMC daemon fully operational");

    int cpu_idx = find_cpu_temp_sensor(&g_state);
    const double poll_interval = 2.0;  /* seconds */
    int cycle = 0;

    while (g_state.running) {
        cycle++;

        pthread_mutex_lock(&g_state.lock);

        /* 1. Poll all sensors */
        sensor_poll(&g_state, g_state.fan_duty_percent);

        /* 2. PID compute new fan duty */
        double cpu_temp = g_state.sensors[cpu_idx].value;
        double new_duty = pid_compute(&g_state.pid,
                                       cpu_temp, poll_interval);
        g_state.fan_duty_percent = new_duty;

        pthread_mutex_unlock(&g_state.lock);

        /* 3. Save state to JSON (for Redfish API) */
        bmc_state_save(&g_state);
        sel_save(&g_state);

        /* Periodic status print */
        if (cycle % 5 == 0) {
            printf("[MAIN] Cycle %d | CPU=%.1f°C | "
                   "Fan=%.1f%% | PID.SP=%.1f°C\n",
                   cycle, cpu_temp, new_duty,
                   g_state.pid.setpoint);
        }

        /* Sleep for polling interval */
        usleep((unsigned int)(poll_interval * 1000000));
    }

    /* ── Shutdown ── */
    printf("\n[MAIN] Shutting down...\n");
    ipmi_stop_listener();
    secure_boot_cleanup(&g_state);
    sel_add_entry(&g_state, SEL_SEVERITY_INFO, "System",
                  "BMC daemon shutting down");
    sel_save(&g_state);
    bmc_state_destroy(&g_state);

    printf("[MAIN] Goodbye!\n");
    return 0;
}
