/**
 * sensor.h - Sensor Simulation & Polling Engine
 *
 * 【學習重點】
 * - 真實 BMC 透過 I2C/SPI bus 讀取硬體 sensor (e.g., TMP75 溫度 IC)
 * - Linux 下透過 sysfs (/sys/class/hwmon/) 或直接 ioctl 存取
 * - 這裡模擬 sensor 行為：基準值 + 隨機 noise + 溫度漂移
 * - Polling 週期通常 1~5 秒，不同 sensor 可有不同 polling rate
 */

#ifndef SENSOR_H
#define SENSOR_H

#include "bmc_state.h"

/**
 * Initialize all simulated sensors with default configurations.
 * Sets up: CPU Temp, Inlet Temp, PCH Temp, 
 *          VCore, V3.3, V12,
 *          CPU Fan, System Fan
 */
int sensor_init(bmc_state_t *state);

/**
 * Poll all sensors once - simulate reading hardware registers.
 * Updates values with noise and thermal model effects.
 *
 * @param state    Global BMC state
 * @param fan_duty Current fan duty cycle (affects temperature simulation)
 */
void sensor_poll(bmc_state_t *state, double fan_duty);

/**
 * Get sensor status string for display/logging.
 */
const char *sensor_status_str(sensor_status_t status);

/**
 * Get sensor type string for display/logging.
 */
const char *sensor_type_str(sensor_type_t type);

#endif /* SENSOR_H */
