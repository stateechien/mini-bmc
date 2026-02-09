/**
 * secure_boot.h - Firmware Secure Boot Verification
 *
 * 【學習重點】
 * - Root of Trust (RoT): 安全信任鏈的起點，通常是硬體 (e.g., Axiado TCU)
 * - Secure Boot 流程: RoT → Bootloader → OS Kernel → Application
 *   每一層驗證下一層的 firmware image hash/signature
 * - 如果任何一層驗證失敗，boot 流程停止（或進入 recovery mode）
 * - Axiado 的 TCU 就是做 hardware-anchored Root of Trust
 *
 * 這裡簡化實作：
 * 1. 生成模擬 firmware images (random data)
 * 2. 計算 SHA-256 hash 作為 "expected" hash
 * 3. 驗證時重新計算 hash 比對
 * 4. 可注入 "tampered" image 來模擬攻擊場景
 *
 * 相關標準：
 * - NIST SP 800-193 (Platform Firmware Resiliency Guidelines)
 * - OCP Caliptra (open-source Root of Trust)
 * - TCG (Trusted Computing Group) TPM 規範
 */

#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#include "bmc_state.h"

/**
 * Initialize secure boot chain with simulated firmware images.
 * Creates dummy firmware blobs and computes expected hashes.
 */
int secure_boot_init(bmc_state_t *state);

/**
 * Verify all firmware images in the boot chain.
 * Computes SHA-256 of each image and compares with expected hash.
 *
 * @return  true if all images pass verification
 */
bool secure_boot_verify(bmc_state_t *state);

/**
 * Simulate tampering with a firmware image (for demo purposes).
 * Modifies the image file so next verification will fail.
 *
 * @param image_index  Index of the firmware image to tamper
 */
int secure_boot_inject_tamper(bmc_state_t *state, int image_index);

/**
 * Restore a tampered firmware image to its original state.
 */
int secure_boot_restore(bmc_state_t *state, int image_index);

/**
 * Cleanup firmware image temp files.
 */
void secure_boot_cleanup(bmc_state_t *state);

#endif /* SECURE_BOOT_H */
