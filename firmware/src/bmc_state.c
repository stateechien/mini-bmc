/**
 * bmc_state.c - BMC State Management & JSON Serialization
 *
 * 【學習重點】
 * - json-c 函式庫：C 語言中處理 JSON 的標準做法
 * - 真實 OpenBMC 用 D-Bus 做 IPC，這裡用 JSON file 簡化
 * - pthread_mutex 保護共享狀態，避免 race condition
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

#include "bmc_state.h"
#include "sensor.h"
#include "event_log.h"

int bmc_state_init(bmc_state_t *state)
{
    memset(state, 0, sizeof(bmc_state_t));

    if (pthread_mutex_init(&state->lock, NULL) != 0) {
        fprintf(stderr, "[STATE] Failed to init mutex\n");
        return -1;
    }

    state->running      = true;
    state->sel_next_id  = 1;
    state->fan_duty_percent = 30.0;  /* Start at 30% */

    printf("[STATE] BMC state initialized\n");
    return 0;
}

/**
 * Serialize a single sensor reading to JSON object.
 */
static json_object *sensor_to_json(const sensor_reading_t *s)
{
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "name",
        json_object_new_string(s->name));
    json_object_object_add(obj, "type",
        json_object_new_string(sensor_type_str(s->type)));
    json_object_object_add(obj, "value",
        json_object_new_double(s->value));
    json_object_object_add(obj, "status",
        json_object_new_string(sensor_status_str(s->status)));
    json_object_object_add(obj, "min_valid",
        json_object_new_double(s->min_valid));
    json_object_object_add(obj, "max_warning",
        json_object_new_double(s->max_warning));
    json_object_object_add(obj, "max_critical",
        json_object_new_double(s->max_critical));
    json_object_object_add(obj, "last_updated",
        json_object_new_int64((int64_t)s->last_updated));
    return obj;
}

/**
 * Serialize PID state to JSON object.
 */
static json_object *pid_to_json(const pid_state_t *pid)
{
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "kp",
        json_object_new_double(pid->kp));
    json_object_object_add(obj, "ki",
        json_object_new_double(pid->ki));
    json_object_object_add(obj, "kd",
        json_object_new_double(pid->kd));
    json_object_object_add(obj, "setpoint",
        json_object_new_double(pid->setpoint));
    json_object_object_add(obj, "output",
        json_object_new_double(pid->output));
    json_object_object_add(obj, "integral",
        json_object_new_double(pid->integral));
    json_object_object_add(obj, "prev_error",
        json_object_new_double(pid->prev_error));
    return obj;
}

/**
 * Serialize firmware image info to JSON object.
 */
static json_object *fw_image_to_json(const fw_image_t *fw)
{
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "name",
        json_object_new_string(fw->name));
    json_object_object_add(obj, "expected_hash",
        json_object_new_string(fw->expected_hash));
    json_object_object_add(obj, "actual_hash",
        json_object_new_string(fw->actual_hash));
    json_object_object_add(obj, "verified",
        json_object_new_boolean(fw->verified));
    json_object_object_add(obj, "passed",
        json_object_new_boolean(fw->passed));
    return obj;
}

int bmc_state_save(bmc_state_t *state)
{
    pthread_mutex_lock(&state->lock);

    json_object *root = json_object_new_object();

    /* ── Sensors ── */
    json_object *sensors = json_object_new_array();
    for (int i = 0; i < state->sensor_count; i++) {
        json_object_array_add(sensors,
            sensor_to_json(&state->sensors[i]));
    }
    json_object_object_add(root, "sensors", sensors);

    /* ── Thermal / PID ── */
    json_object *thermal = json_object_new_object();
    json_object_object_add(thermal, "fan_duty_percent",
        json_object_new_double(state->fan_duty_percent));
    json_object_object_add(thermal, "pid", pid_to_json(&state->pid));
    json_object_object_add(root, "thermal", thermal);

    /* ── Secure Boot ── */
    json_object *secboot = json_object_new_object();
    json_object_object_add(secboot, "overall_passed",
        json_object_new_boolean(state->secure_boot_passed));
    json_object *images = json_object_new_array();
    for (int i = 0; i < state->fw_image_count; i++) {
        json_object_array_add(images,
            fw_image_to_json(&state->fw_images[i]));
    }
    json_object_object_add(secboot, "images", images);
    json_object_object_add(root, "secure_boot", secboot);

    /* ── Write to file atomically (write tmp then rename) ── */
    const char *tmp_path = STATE_FILE_PATH ".tmp";
    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        fprintf(stderr, "[STATE] Failed to open %s for writing\n",
                tmp_path);
        json_object_put(root);
        pthread_mutex_unlock(&state->lock);
        return -1;
    }

    const char *json_str = json_object_to_json_string_ext(root,
        JSON_C_TO_STRING_PRETTY);
    fprintf(fp, "%s\n", json_str);
    fclose(fp);

    /* Atomic rename to avoid partial reads by the Python API */
    rename(tmp_path, STATE_FILE_PATH);

    json_object_put(root);
    pthread_mutex_unlock(&state->lock);
    return 0;
}

void bmc_state_destroy(bmc_state_t *state)
{
    state->running = false;
    pthread_mutex_destroy(&state->lock);
    /* Remove temp files */
    remove(STATE_FILE_PATH);
    remove(SEL_FILE_PATH);
    printf("[STATE] BMC state destroyed\n");
}
