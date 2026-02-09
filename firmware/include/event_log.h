/**
 * event_log.h - System Event Log (SEL)
 *
 * 【學習重點】
 * - SEL 是 IPMI 定義的事件記錄機制
 * - 記錄 sensor threshold crossings、系統錯誤、firmware events
 * - 真實 BMC 將 SEL 存在 EEPROM 或 flash 中，斷電不消失
 * - OpenBMC 用 phosphor-logging 服務管理 event log
 * - Redfish 透過 /redfish/v1/Managers/1/LogServices/SEL 暴露
 */

#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "bmc_state.h"

/**
 * Initialize event log system.
 */
int sel_init(bmc_state_t *state);

/**
 * Add a new event log entry.
 *
 * @param state     Global BMC state
 * @param severity  Event severity level
 * @param source    Module that generated the event (e.g., "Sensor", "PID")
 * @param fmt       Printf-style format string for the message
 * @return          Entry ID or -1 on error
 */
int sel_add_entry(bmc_state_t *state, sel_severity_t severity,
                  const char *source, const char *fmt, ...);

/**
 * Get event log entry by ID.
 *
 * @return  Pointer to entry or NULL if not found
 */
const sel_entry_t *sel_get_entry(bmc_state_t *state, uint32_t id);

/**
 * Save SEL to persistent JSON file.
 */
int sel_save(bmc_state_t *state);

/**
 * Get severity string.
 */
const char *sel_severity_str(sel_severity_t sev);

#endif /* EVENT_LOG_H */
