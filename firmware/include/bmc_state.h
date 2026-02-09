/**
 * bmc_state.h - BMC Shared State Management
 *
 * 【學習重點】
 * - BMC 韌體中各模組需要共享狀態（sensor data, fan speed, events）
 * - 真實 BMC 用 D-Bus (OpenBMC) 或 shared memory 做 IPC
 * - 這裡用 JSON file 簡化，概念相同：firmware 寫、management SW 讀
 */

#ifndef BMC_STATE_H
#define BMC_STATE_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ── Sensor Definitions ─────────────────────────────── */

#define MAX_SENSORS       8
#define MAX_SEL_ENTRIES   256
#define MAX_FW_IMAGES     4
#define STATE_FILE_PATH   "/tmp/bmc_state.json"
#define SEL_FILE_PATH     "/tmp/bmc_sel.json"

typedef enum {
    SENSOR_TYPE_TEMPERATURE,   /* Celsius */
    SENSOR_TYPE_VOLTAGE,       /* Volts */
    SENSOR_TYPE_FAN_RPM,       /* RPM */
    SENSOR_TYPE_POWER          /* Watts */
} sensor_type_t;

typedef enum {
    SENSOR_STATUS_OK,
    SENSOR_STATUS_WARNING,
    SENSOR_STATUS_CRITICAL,
    SENSOR_STATUS_ABSENT
} sensor_status_t;

typedef struct {
    char        name[64];
    sensor_type_t type;
    double      value;
    double      min_valid;      /* Lower bound for OK status */
    double      max_warning;    /* Warning threshold */
    double      max_critical;   /* Critical threshold */
    sensor_status_t status;
    time_t      last_updated;
} sensor_reading_t;

/* ── PID Controller State ───────────────────────────── */

typedef struct {
    double kp;              /* Proportional gain */
    double ki;              /* Integral gain */
    double kd;              /* Derivative gain */
    double setpoint;        /* Target temperature (°C) */
    double integral;        /* Accumulated integral term */
    double prev_error;      /* Previous error for derivative */
    double output;          /* Current output (fan duty %) */
    double output_min;      /* Min output clamp */
    double output_max;      /* Max output clamp */
} pid_state_t;

/* ── System Event Log (SEL) ─────────────────────────── */

typedef enum {
    SEL_SEVERITY_INFO,
    SEL_SEVERITY_WARNING,
    SEL_SEVERITY_CRITICAL
} sel_severity_t;

typedef struct {
    uint32_t        id;
    time_t          timestamp;
    sel_severity_t  severity;
    char            source[32];
    char            message[256];
} sel_entry_t;

/* ── Secure Boot State ──────────────────────────────── */

typedef struct {
    char    name[64];
    char    expected_hash[65];  /* SHA-256 hex string */
    char    actual_hash[65];
    bool    verified;
    bool    passed;
} fw_image_t;

/* ── Global BMC State ───────────────────────────────── */

typedef struct {
    /* Sensors */
    sensor_reading_t sensors[MAX_SENSORS];
    int              sensor_count;

    /* Thermal PID */
    pid_state_t      pid;
    double           fan_duty_percent;

    /* Event Log */
    sel_entry_t      sel[MAX_SEL_ENTRIES];
    int              sel_count;
    uint32_t         sel_next_id;

    /* Secure Boot */
    fw_image_t       fw_images[MAX_FW_IMAGES];
    int              fw_image_count;
    bool             secure_boot_passed;

    /* Daemon control */
    bool             running;
    pthread_mutex_t  lock;
} bmc_state_t;

/* ── Functions ──────────────────────────────────────── */

/**
 * Initialize BMC state with default sensor configuration.
 */
int bmc_state_init(bmc_state_t *state);

/**
 * Serialize current state to JSON file for Redfish API consumption.
 */
int bmc_state_save(bmc_state_t *state);

/**
 * Cleanup resources.
 */
void bmc_state_destroy(bmc_state_t *state);

#endif /* BMC_STATE_H */
