/**
 * secure_boot.c - Firmware Secure Boot Verification Chain
 *
 * 【學習重點 - Root of Trust & Axiado TCU】
 *
 * Secure Boot Chain of Trust:
 *   Hardware RoT (Axiado TCU)
 *     → verifies Bootloader hash
 *       → verifies BMC Firmware hash
 *         → verifies Application hash
 *           → verifies Config hash
 *
 * 每一層用前一層已驗證的公鑰/hash 驗證下一層
 * 如果任何一環被篡改 (tamper)，整條鏈斷裂 → 拒絕啟動
 *
 * 真實實作用 RSA/ECDSA 數位簽章，這裡簡化為 SHA-256 hash 比對
 * 但概念完全相同
 *
 * Axiado 的 TCU 做的事：
 * 1. 硬體層級的 Root of Trust (不可篡改)
 * 2. AI-driven 異常偵測 (偵測 firmware 被竄改)
 * 3. Runtime attestation (不只開機時驗，持續驗)
 *
 * 面試時可以提：
 * "我理解 Root of Trust 的 chain-of-trust 概念，
 *  並在 mini-BMC 專案中實作了簡化版的 secure boot verification。
 *  我知道 Axiado 的 TCU 提供 hardware-anchored RoT，
 *  結合 AI 做 runtime firmware attestation。"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

#include "secure_boot.h"
#include "event_log.h"

#define FW_IMAGE_DIR    "/tmp/bmc_fw_images"
#define FW_IMAGE_SIZE   4096  /* Simulated firmware image size */

/* ── Firmware image configuration ── */

typedef struct {
    const char *name;
    const char *description;
} fw_config_t;

static const fw_config_t fw_configs[] = {
    { "bootloader",   "First-stage bootloader (RoT verified)" },
    { "bmc_firmware",  "BMC main firmware image" },
    { "application",   "Management application layer" },
    { "config_data",   "Platform configuration data" },
};

#define NUM_FW_IMAGES (sizeof(fw_configs) / sizeof(fw_configs[0]))

/**
 * Compute SHA-256 hash of a file and return hex string.
 */
static int compute_file_hash(const char *filepath, char *hash_out)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    unsigned char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        SHA256_Update(&ctx, buf, n);
    }
    fclose(fp);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hash_out + (i * 2), "%02x", digest[i]);
    }
    hash_out[SHA256_DIGEST_LENGTH * 2] = '\0';

    return 0;
}

/**
 * Generate a simulated firmware image file with deterministic content.
 */
static int generate_fw_image(const char *filepath, int seed)
{
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -1;

    /* Generate deterministic "firmware" content */
    srand(seed);
    unsigned char buf[FW_IMAGE_SIZE];
    for (int i = 0; i < FW_IMAGE_SIZE; i++) {
        buf[i] = (unsigned char)(rand() % 256);
    }

    fwrite(buf, 1, FW_IMAGE_SIZE, fp);
    fclose(fp);
    return 0;
}

int secure_boot_init(bmc_state_t *state)
{
    /* Create firmware image directory */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", FW_IMAGE_DIR);
    system(cmd);

    state->fw_image_count = NUM_FW_IMAGES;
    if (state->fw_image_count > MAX_FW_IMAGES) {
        state->fw_image_count = MAX_FW_IMAGES;
    }

    for (int i = 0; i < state->fw_image_count; i++) {
        fw_image_t *fw = &state->fw_images[i];
        strncpy(fw->name, fw_configs[i].name, sizeof(fw->name) - 1);
        fw->verified = false;
        fw->passed   = false;

        /* Generate firmware image file */
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.bin",
                 FW_IMAGE_DIR, fw->name);
        generate_fw_image(path, 42 + i);  /* Deterministic seed */

        /* Compute and store expected hash */
        compute_file_hash(path, fw->expected_hash);

        printf("[SECBOOT] Image '%s' hash: %.16s...\n",
               fw->name, fw->expected_hash);
    }

    sel_add_entry(state, SEL_SEVERITY_INFO, "SecureBoot",
                  "Secure boot chain initialized with %d images",
                  state->fw_image_count);

    printf("[SECBOOT] Initialized %d firmware images\n",
           state->fw_image_count);
    return 0;
}

bool secure_boot_verify(bmc_state_t *state)
{
    bool all_passed = true;

    printf("[SECBOOT] ═══ Starting Secure Boot Verification ═══\n");

    for (int i = 0; i < state->fw_image_count; i++) {
        fw_image_t *fw = &state->fw_images[i];

        char path[256];
        snprintf(path, sizeof(path), "%s/%s.bin",
                 FW_IMAGE_DIR, fw->name);

        /* Compute current hash */
        if (compute_file_hash(path, fw->actual_hash) != 0) {
            fw->verified = true;
            fw->passed   = false;
            all_passed   = false;

            sel_add_entry(state, SEL_SEVERITY_CRITICAL, "SecureBoot",
                "FAIL: Cannot read image '%s'", fw->name);
            printf("[SECBOOT] ✗ %s: FILE NOT FOUND\n", fw->name);
            continue;
        }

        fw->verified = true;
        fw->passed = (strcmp(fw->expected_hash, fw->actual_hash) == 0);

        if (fw->passed) {
            printf("[SECBOOT] ✓ %s: VERIFIED\n", fw->name);
            sel_add_entry(state, SEL_SEVERITY_INFO, "SecureBoot",
                "PASS: Image '%s' integrity verified", fw->name);
        } else {
            all_passed = false;
            printf("[SECBOOT] ✗ %s: HASH MISMATCH!\n", fw->name);
            printf("  Expected: %.16s...\n", fw->expected_hash);
            printf("  Actual:   %.16s...\n", fw->actual_hash);
            sel_add_entry(state, SEL_SEVERITY_CRITICAL, "SecureBoot",
                "FAIL: Image '%s' hash mismatch - possible tampering!",
                fw->name);
        }

        /* Chain of trust: if this stage fails, don't verify further */
        if (!fw->passed) {
            printf("[SECBOOT] Chain broken at '%s', halting.\n",
                   fw->name);
            break;
        }
    }

    state->secure_boot_passed = all_passed;

    printf("[SECBOOT] ═══ Verification %s ═══\n",
           all_passed ? "PASSED" : "FAILED");

    return all_passed;
}

int secure_boot_inject_tamper(bmc_state_t *state, int image_index)
{
    if (image_index < 0 || image_index >= state->fw_image_count)
        return -1;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.bin",
             FW_IMAGE_DIR, state->fw_images[image_index].name);

    /* Modify one byte in the firmware image to simulate tampering */
    FILE *fp = fopen(path, "r+b");
    if (!fp) return -1;

    unsigned char byte = 0xFF;
    fwrite(&byte, 1, 1, fp);  /* Corrupt first byte */
    fclose(fp);

    sel_add_entry(state, SEL_SEVERITY_WARNING, "SecureBoot",
        "[DEMO] Injected tamper into '%s'",
        state->fw_images[image_index].name);

    printf("[SECBOOT] Tamper injected into '%s'\n",
           state->fw_images[image_index].name);
    return 0;
}

int secure_boot_restore(bmc_state_t *state, int image_index)
{
    if (image_index < 0 || image_index >= state->fw_image_count)
        return -1;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s.bin",
             FW_IMAGE_DIR, state->fw_images[image_index].name);

    /* Regenerate original image */
    generate_fw_image(path, 42 + image_index);

    sel_add_entry(state, SEL_SEVERITY_INFO, "SecureBoot",
        "[DEMO] Restored image '%s'",
        state->fw_images[image_index].name);

    printf("[SECBOOT] Image '%s' restored\n",
           state->fw_images[image_index].name);
    return 0;
}

void secure_boot_cleanup(bmc_state_t *state)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", FW_IMAGE_DIR);
    system(cmd);
    (void)state;
    printf("[SECBOOT] Cleaned up firmware images\n");
}
