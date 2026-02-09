/**
 * ipmi.c - Simplified IPMI Command Handler
 *
 * 【學習重點 - IPMI 協議架構】
 *
 * IPMI Message Format (簡化):
 *   Request:  [NetFn] [CMD] [Data...]
 *   Response: [Completion Code] [Data...]
 *
 * 真實 IPMI 傳輸介面:
 *   - KCS (Keyboard Controller Style): Host CPU ↔ BMC 透過 LPC bus
 *   - BT (Block Transfer): 較高頻寬
 *   - RMCP/RMCP+: 網路遠端管理 (UDP port 623)
 *   - SSIF (SMBus System Interface): 透過 SMBus
 *
 * OpenBMC 中的 ipmid:
 *   - D-Bus service 接收 IPMI 命令
 *   - 用 handler registration 機制分派命令
 *   - 跟 phosphor-host-ipmid (KCS) 和
 *     phosphor-net-ipmid (RMCP+) 配合
 *
 * 這裡用 Unix Domain Socket 模擬，概念相同
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include "ipmi.h"
#include "event_log.h"

#define IPMI_SOCKET_PATH "/tmp/bmc_ipmi.sock"

static int server_fd = -1;
static pthread_t listener_thread;
static bmc_state_t *g_state = NULL;

/* ── Command Handlers ── */

static void handle_get_device_id(bmc_state_t *state,
                                  ipmi_response_t *resp)
{
    (void)state;
    resp->completion_code = IPMI_CC_OK;
    /* Simplified Device ID response */
    resp->data[0] = 0x20;   /* Device ID */
    resp->data[1] = 0x01;   /* Device Revision */
    resp->data[2] = 0x02;   /* Firmware Major Rev */
    resp->data[3] = 0x05;   /* Firmware Minor Rev */
    resp->data[4] = 0x02;   /* IPMI Version 2.0 */
    resp->data_len = 5;
}

static void handle_get_sensor_reading(bmc_state_t *state,
                                       const ipmi_request_t *req,
                                       ipmi_response_t *resp)
{
    if (req->data_len < 1) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        return;
    }

    uint8_t sensor_num = req->data[0];
    if (sensor_num >= state->sensor_count) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        return;
    }

    pthread_mutex_lock(&state->lock);
    sensor_reading_t *s = &state->sensors[sensor_num];

    resp->completion_code = IPMI_CC_OK;

    /* Pack sensor value as 16-bit fixed point (8.8 format) */
    int16_t raw = (int16_t)(s->value * 256.0);
    resp->data[0] = (uint8_t)(raw >> 8);    /* Integer part */
    resp->data[1] = (uint8_t)(raw & 0xFF);  /* Fractional part */
    resp->data[2] = (uint8_t)s->status;
    resp->data[3] = (uint8_t)s->type;
    resp->data_len = 4;

    pthread_mutex_unlock(&state->lock);
}

static void handle_set_fan_duty(bmc_state_t *state,
                                 const ipmi_request_t *req,
                                 ipmi_response_t *resp)
{
    if (req->data_len < 1) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        return;
    }

    double duty = (double)req->data[0];
    if (duty < 0 || duty > 100) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        return;
    }

    pthread_mutex_lock(&state->lock);
    state->fan_duty_percent = duty;
    pthread_mutex_unlock(&state->lock);

    sel_add_entry(state, SEL_SEVERITY_INFO, "IPMI",
        "Fan duty manually set to %.0f%%", duty);

    resp->completion_code = IPMI_CC_OK;
    resp->data_len = 0;
}

static void handle_get_sel_entry(bmc_state_t *state,
                                  const ipmi_request_t *req,
                                  ipmi_response_t *resp)
{
    if (req->data_len < 2) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        return;
    }

    uint16_t entry_id = (req->data[0] << 8) | req->data[1];

    pthread_mutex_lock(&state->lock);
    const sel_entry_t *entry = sel_get_entry(state, entry_id);

    if (!entry) {
        resp->completion_code = IPMI_CC_INVALID_PARAM;
        pthread_mutex_unlock(&state->lock);
        return;
    }

    resp->completion_code = IPMI_CC_OK;
    /* Pack SEL entry data */
    resp->data[0] = (uint8_t)(entry->id >> 8);
    resp->data[1] = (uint8_t)(entry->id & 0xFF);
    resp->data[2] = (uint8_t)entry->severity;

    /* Copy truncated message */
    int msg_len = strlen(entry->message);
    if (msg_len > 200) msg_len = 200;
    memcpy(&resp->data[3], entry->message, msg_len);
    resp->data_len = 3 + msg_len;

    pthread_mutex_unlock(&state->lock);
}

/* ── Command Dispatcher ── */

int ipmi_handle_command(bmc_state_t *state,
                        const ipmi_request_t *req,
                        ipmi_response_t *resp)
{
    memset(resp, 0, sizeof(ipmi_response_t));

    switch (req->netfn) {
    case IPMI_NETFN_APP:
        if (req->cmd == IPMI_CMD_GET_DEVICE_ID) {
            handle_get_device_id(state, resp);
        } else {
            resp->completion_code = IPMI_CC_INVALID_CMD;
        }
        break;

    case IPMI_NETFN_SENSOR:
        if (req->cmd == IPMI_CMD_GET_SENSOR_READING) {
            handle_get_sensor_reading(state, req, resp);
        } else if (req->cmd == IPMI_CMD_SET_FAN_DUTY) {
            handle_set_fan_duty(state, req, resp);
        } else {
            resp->completion_code = IPMI_CC_INVALID_CMD;
        }
        break;

    case IPMI_NETFN_STORAGE:
        if (req->cmd == IPMI_CMD_GET_SEL_ENTRY) {
            handle_get_sel_entry(state, req, resp);
        } else {
            resp->completion_code = IPMI_CC_INVALID_CMD;
        }
        break;

    default:
        resp->completion_code = IPMI_CC_INVALID_CMD;
        break;
    }

    return 0;
}

/* ── Socket Listener ── */

static void *ipmi_listener_fn(void *arg)
{
    (void)arg;
    printf("[IPMI] Listener started on %s\n", IPMI_SOCKET_PATH);

    while (g_state && g_state->running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
            (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        /* Read IPMI request */
        ipmi_request_t req;
        memset(&req, 0, sizeof(req));

        ssize_t n = read(client_fd, &req, sizeof(req));
        if (n > 0) {
            ipmi_response_t resp;
            ipmi_handle_command(g_state, &req, &resp);
            write(client_fd, &resp, sizeof(resp));
        }

        close(client_fd);
    }

    return NULL;
}

int ipmi_start_listener(bmc_state_t *state)
{
    g_state = state;

    /* Remove stale socket file */
    unlink(IPMI_SOCKET_PATH);

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[IPMI] socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPMI_SOCKET_PATH,
            sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[IPMI] bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("[IPMI] listen");
        close(server_fd);
        return -1;
    }

    pthread_create(&listener_thread, NULL, ipmi_listener_fn, NULL);
    return 0;
}

void ipmi_stop_listener(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(IPMI_SOCKET_PATH);
    printf("[IPMI] Listener stopped\n");
}
