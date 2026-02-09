/**
 * sensor.c - Sensor Simulation & Polling Engine
 *
 * 【學習重點 - I2C/SPI Sensor 讀取】
 *
 * 真實 BMC 的 sensor 讀取流程:
 * 1. 透過 I2C bus 讀取溫度 IC (e.g., TMP75 address 0x48)
 *    fd = open("/dev/i2c-0", O_RDWR);
 *    ioctl(fd, I2C_SLAVE, 0x48);
 *    read(fd, buf, 2);  // 2 bytes = 溫度值
 *
 * 2. 透過 ADC 讀取電壓
 *    read /sys/bus/iio/devices/iio:device0/in_voltage0_raw
 *
 * 3. 透過 PWM/Tach 讀取風扇 RPM
 *    read /sys/class/hwmon/hwmon0/fan1_input
 *
 * 這裡模擬這些硬體行為：
 * - 溫度 = 基準值 + 熱模型效應(受風扇影響) + Gaussian noise
 * - 電壓 = 額定值 + 小幅隨機波動
 * - 風扇 RPM = duty cycle × max RPM + noise
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "sensor.h"
#include "event_log.h"

/* ── Helper: Gaussian noise using Box-Muller transform ── */
static double gaussian_noise(double mean, double stddev)
{
    static int has_spare = 0;
    static double spare;

    if (has_spare) {
        has_spare = 0;
        return mean + stddev * spare;
    }

    has_spare = 1;
    double u, v, s;
    do {
        u = (rand() / (double)RAND_MAX) * 2.0 - 1.0;
        v = (rand() / (double)RAND_MAX) * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);

    s = sqrt(-2.0 * log(s) / s);
    spare = v * s;
    return mean + stddev * u * s;
}

/* ── Sensor Configuration ── */

typedef struct {
    const char  *name;
    sensor_type_t type;
    double      base_value;
    double      noise_stddev;
    double      min_valid;
    double      max_warning;
    double      max_critical;
} sensor_config_t;

static const sensor_config_t default_sensors[] = {
    /* Temperature sensors */
    { "CPU_Temp",    SENSOR_TYPE_TEMPERATURE, 55.0, 1.5,
      10.0, 75.0, 90.0 },
    { "Inlet_Temp",  SENSOR_TYPE_TEMPERATURE, 28.0, 0.8,
      5.0,  38.0, 45.0 },
    { "PCH_Temp",    SENSOR_TYPE_TEMPERATURE, 48.0, 1.0,
      10.0, 70.0, 85.0 },

    /* Voltage sensors */
    { "VCore",       SENSOR_TYPE_VOLTAGE,     1.05, 0.02,
      0.90, 1.15, 1.25 },
    { "V3.3_Stdby",  SENSOR_TYPE_VOLTAGE,     3.30, 0.03,
      3.10, 3.50, 3.60 },
    { "V12_Main",    SENSOR_TYPE_VOLTAGE,    12.00, 0.08,
      11.40, 12.60, 13.00 },

    /* Fan sensors (RPM) */
    { "CPU_Fan",     SENSOR_TYPE_FAN_RPM,   3000.0, 50.0,
      500.0, 6000.0, 7000.0 },
    { "SYS_Fan",     SENSOR_TYPE_FAN_RPM,   2500.0, 40.0,
      400.0, 5000.0, 6000.0 },
};

#define NUM_DEFAULT_SENSORS \
    (sizeof(default_sensors) / sizeof(default_sensors[0]))

/* ── Helper functions ── */

const char *sensor_status_str(sensor_status_t status)
{
    switch (status) {
    case SENSOR_STATUS_OK:       return "OK";
    case SENSOR_STATUS_WARNING:  return "Warning";
    case SENSOR_STATUS_CRITICAL: return "Critical";
    case SENSOR_STATUS_ABSENT:   return "Absent";
    default:                     return "Unknown";
    }
}

const char *sensor_type_str(sensor_type_t type)
{
    switch (type) {
    case SENSOR_TYPE_TEMPERATURE: return "Temperature";
    case SENSOR_TYPE_VOLTAGE:     return "Voltage";
    case SENSOR_TYPE_FAN_RPM:     return "Fan";
    case SENSOR_TYPE_POWER:       return "Power";
    default:                      return "Unknown";
    }
}

static sensor_status_t evaluate_status(const sensor_reading_t *s)
{
    if (s->type == SENSOR_TYPE_FAN_RPM) {
        /* Fan: low RPM is bad */
        if (s->value < s->min_valid)     return SENSOR_STATUS_CRITICAL;
        if (s->value > s->max_critical)  return SENSOR_STATUS_CRITICAL;
        if (s->value > s->max_warning)   return SENSOR_STATUS_WARNING;
        return SENSOR_STATUS_OK;
    }

    /* Temperature / Voltage: high value is bad */
    if (s->value >= s->max_critical) return SENSOR_STATUS_CRITICAL;
    if (s->value >= s->max_warning)  return SENSOR_STATUS_WARNING;
    if (s->value < s->min_valid)     return SENSOR_STATUS_WARNING;
    return SENSOR_STATUS_OK;
}

/* ── Public API ── */

int sensor_init(bmc_state_t *state)
{
    srand((unsigned int)time(NULL));

    state->sensor_count = NUM_DEFAULT_SENSORS;
    if (state->sensor_count > MAX_SENSORS) {
        state->sensor_count = MAX_SENSORS;
    }

    for (int i = 0; i < state->sensor_count; i++) {
        const sensor_config_t *cfg = &default_sensors[i];
        sensor_reading_t *s = &state->sensors[i];

        strncpy(s->name, cfg->name, sizeof(s->name) - 1);
        s->type         = cfg->type;
        s->value        = cfg->base_value;
        s->min_valid    = cfg->min_valid;
        s->max_warning  = cfg->max_warning;
        s->max_critical = cfg->max_critical;
        s->status       = SENSOR_STATUS_OK;
        s->last_updated = time(NULL);
    }

    printf("[SENSOR] Initialized %d sensors\n", state->sensor_count);
    return 0;
}

void sensor_poll(bmc_state_t *state, double fan_duty)
{
    time_t now = time(NULL);

    for (int i = 0; i < state->sensor_count; i++) {
        sensor_reading_t *s = &state->sensors[i];
        const sensor_config_t *cfg = &default_sensors[i];
        sensor_status_t old_status = s->status;

        switch (s->type) {
        case SENSOR_TYPE_TEMPERATURE: {
            /*
             * Thermal model:
             * - Base temp + workload heat
             * - Fan cooling effect: higher duty → lower temp
             * - T = T_base + heat_load - cooling_effect + noise
             *
             * cooling = fan_duty/100 × cooling_capacity
             * This models the physical relationship between
             * airflow and heat dissipation.
             */
            double heat_load     = 15.0;  /* Simulated CPU workload */
            double cool_capacity = 25.0;  /* Max cooling at 100% fan */
            double cooling = (fan_duty / 100.0) * cool_capacity;

            /* Slow drift toward equilibrium (first-order response) */
            double target = cfg->base_value + heat_load - cooling;
            s->value += (target - s->value) * 0.1;  /* tau ≈ 10 cycles */
            s->value += gaussian_noise(0, cfg->noise_stddev);

            /* Clamp to physical limits */
            if (s->value < 5.0)   s->value = 5.0;
            if (s->value > 105.0) s->value = 105.0;
            break;
        }

        case SENSOR_TYPE_VOLTAGE:
            /* Voltage: small random fluctuation around nominal */
            s->value = cfg->base_value +
                       gaussian_noise(0, cfg->noise_stddev);
            if (s->value < 0) s->value = 0;
            break;

        case SENSOR_TYPE_FAN_RPM: {
            /*
             * Fan RPM = (duty/100) × max_rpm + noise
             * max_rpm is roughly 2× the base value
             */
            double max_rpm = cfg->base_value * 2.0;
            s->value = (fan_duty / 100.0) * max_rpm +
                       gaussian_noise(0, cfg->noise_stddev);
            if (s->value < 0) s->value = 0;
            break;
        }

        default:
            break;
        }

        s->last_updated = now;
        s->status = evaluate_status(s);

        /* Log status transitions */
        if (s->status != old_status && s->status != SENSOR_STATUS_OK) {
            sel_severity_t sev = (s->status == SENSOR_STATUS_CRITICAL)
                ? SEL_SEVERITY_CRITICAL : SEL_SEVERITY_WARNING;
            sel_add_entry(state, sev, "Sensor",
                "%s transitioned to %s (value: %.2f)",
                s->name, sensor_status_str(s->status), s->value);
        }
    }
}
