/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  LittleFS IO configurations.
 *         The LittleFS partition must be mounted via esp_vfs_littlefs_register()
 *         before calling open. All file operations use standard POSIX stdio.
 */
typedef struct {
    int         dir;   /*!< IO direction, reader or writer */
    const char *name;  /*!< Name for this instance */
} lfs2_io_cfg_t;

#define LFS2_IO_CFG_DEFAULT() {    \
    .dir  = ESP_GMF_IO_DIR_READER, \
    .name = NULL,                  \
}

/**
 * @brief  Initializes the LittleFS flash filesystem I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the LFS2 IO configuration
 * @param[out]  io      Pointer to the IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_lfs2_init(lfs2_io_cfg_t *config, esp_gmf_io_handle_t *io);

#ifdef __cplusplus
}
#endif /* __cplusplus */
