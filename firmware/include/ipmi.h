/**
 * ipmi.h - Simplified IPMI Command Handler
 *
 * 【學習重點】
 * - IPMI (Intelligent Platform Management Interface) 是伺服器管理標準
 * - 定義了 NetFn (Network Function) + Command 的命令結構
 * - 常見 NetFn: Sensor/Event (0x04), App (0x06), Storage (0x0A)
 * - 真實 BMC 處理來自 host (KCS interface) 或 network (RMCP+) 的 IPMI 命令
 * - OpenBMC 用 ipmid daemon 處理這些命令
 *
 * 這裡實作簡化版：透過 Unix Domain Socket 接收命令
 */

#ifndef IPMI_H
#define IPMI_H

#include "bmc_state.h"

/* IPMI Network Functions (simplified) */
#define IPMI_NETFN_SENSOR   0x04
#define IPMI_NETFN_APP      0x06
#define IPMI_NETFN_STORAGE  0x0A

/* IPMI Commands */
#define IPMI_CMD_GET_DEVICE_ID      0x01  /* NetFn App */
#define IPMI_CMD_GET_SENSOR_READING 0x2D  /* NetFn Sensor */
#define IPMI_CMD_SET_FAN_DUTY       0x30  /* Custom: set fan duty */
#define IPMI_CMD_GET_SEL_ENTRY      0x43  /* NetFn Storage */

/* IPMI Completion Codes */
#define IPMI_CC_OK                  0x00
#define IPMI_CC_INVALID_CMD         0xC1
#define IPMI_CC_INVALID_PARAM       0xC9
#define IPMI_CC_UNSPECIFIED         0xFF

/* Request/Response structures */
typedef struct {
    uint8_t netfn;
    uint8_t cmd;
    uint8_t data[256];
    uint8_t data_len;
} ipmi_request_t;

typedef struct {
    uint8_t completion_code;
    uint8_t data[256];
    uint8_t data_len;
} ipmi_response_t;

/**
 * Process an IPMI command and generate response.
 */
int ipmi_handle_command(bmc_state_t *state,
                        const ipmi_request_t *req,
                        ipmi_response_t *resp);

/**
 * Start IPMI listener thread (Unix Domain Socket).
 */
int ipmi_start_listener(bmc_state_t *state);

/**
 * Stop IPMI listener.
 */
void ipmi_stop_listener(void);

#endif /* IPMI_H */
