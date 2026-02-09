/**
 * event_log.c - System Event Log (SEL) Implementation
 *
 * 【學習重點】
 * - SEL 是 IPMI spec 定義的標準 event logging 機制
 * - 每條 entry 有: record ID, timestamp, sensor type, event data
 * - 真實 BMC 的 SEL 存在 non-volatile storage (EEPROM/Flash)
 * - OpenBMC 用 phosphor-logging + D-Bus 管理 log entries
 * - Redfish 暴露為 /redfish/v1/Managers/1/LogServices/SEL/Entries
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <json-c/json.h>

#include "event_log.h"

const char *sel_severity_str(sel_severity_t sev)
{
    switch (sev) {
    case SEL_SEVERITY_INFO:     return "Info";
    case SEL_SEVERITY_WARNING:  return "Warning";
    case SEL_SEVERITY_CRITICAL: return "Critical";
    default:                    return "Unknown";
    }
}

int sel_init(bmc_state_t *state)
{
    state->sel_count   = 0;
    state->sel_next_id = 1;
    printf("[SEL] Event log initialized (max %d entries)\n",
           MAX_SEL_ENTRIES);
    return 0;
}

int sel_add_entry(bmc_state_t *state, sel_severity_t severity,
                  const char *source, const char *fmt, ...)
{
    if (state->sel_count >= MAX_SEL_ENTRIES) {
        /* Circular buffer: overwrite oldest */
        memmove(&state->sel[0], &state->sel[1],
                (MAX_SEL_ENTRIES - 1) * sizeof(sel_entry_t));
        state->sel_count = MAX_SEL_ENTRIES - 1;
    }

    sel_entry_t *entry = &state->sel[state->sel_count];
    entry->id        = state->sel_next_id++;
    entry->timestamp = time(NULL);
    entry->severity  = severity;

    strncpy(entry->source, source, sizeof(entry->source) - 1);
    entry->source[sizeof(entry->source) - 1] = '\0';

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, sizeof(entry->message), fmt, args);
    va_end(args);

    state->sel_count++;

    printf("[SEL] #%u [%s] %s: %s\n",
           entry->id, sel_severity_str(severity),
           source, entry->message);

    /* Auto-save after each critical event */
    if (severity == SEL_SEVERITY_CRITICAL) {
        sel_save(state);
    }

    return (int)entry->id;
}

const sel_entry_t *sel_get_entry(bmc_state_t *state, uint32_t id)
{
    for (int i = 0; i < state->sel_count; i++) {
        if (state->sel[i].id == id)
            return &state->sel[i];
    }
    return NULL;
}

int sel_save(bmc_state_t *state)
{
    json_object *root = json_object_new_object();
    json_object *entries = json_object_new_array();

    for (int i = 0; i < state->sel_count; i++) {
        sel_entry_t *e = &state->sel[i];
        json_object *obj = json_object_new_object();

        json_object_object_add(obj, "id",
            json_object_new_int(e->id));
        json_object_object_add(obj, "timestamp",
            json_object_new_int64((int64_t)e->timestamp));
        json_object_object_add(obj, "severity",
            json_object_new_string(sel_severity_str(e->severity)));
        json_object_object_add(obj, "source",
            json_object_new_string(e->source));
        json_object_object_add(obj, "message",
            json_object_new_string(e->message));

        json_object_array_add(entries, obj);
    }

    json_object_object_add(root, "entries", entries);
    json_object_object_add(root, "count",
        json_object_new_int(state->sel_count));

    FILE *fp = fopen(SEL_FILE_PATH, "w");
    if (!fp) {
        json_object_put(root);
        return -1;
    }

    fprintf(fp, "%s\n",
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
    fclose(fp);

    json_object_put(root);
    return 0;
}
