/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "errno.h"
#include "esp_log.h"
#include "esp_gmf_io_lfs2.h"
#include "esp_gmf_oal_mem.h"

/**
 * @brief  LittleFS io context in GMF.
 *         Relies on esp_vfs_littlefs_register() having been called by the application
 *         before open is invoked. All file operations use standard POSIX stdio.
 */
typedef struct {
    esp_gmf_io_t  base;     /*!< The GMF io handle */
    bool          is_open;  /*!< Whether the file is open */
    FILE         *file;     /*!< POSIX file handle */
} lfs2_io_stream_t;

static const char *TAG = "ESP_GMF_LFS2";

static char *get_mount_path(char *uri)
{
    /* support format: /littlefs, /spiffs, /sdcard etc ... */
    if (uri[0] == '/') {
        return uri;
    }
    /* support format: scheme://basepath... */
    char *skip_scheme = strstr(uri, "://");
    if (skip_scheme == NULL) {
        return NULL;
    }
    skip_scheme += 2;
    /* support format: scheme:///basepath... */
    if (skip_scheme[1] == '/') {
        skip_scheme++;
    }
    return skip_scheme;
}

static esp_gmf_err_t _lfs2_new(void *cfg, esp_gmf_obj_handle_t *io)
{
    return esp_gmf_io_lfs2_init(cfg, io);
}

static esp_gmf_err_t _lfs2_open(esp_gmf_io_handle_t io)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)io;
    char *uri = NULL;
    esp_gmf_io_get_uri(io, &uri);
    if (uri == NULL) {
        ESP_LOGE(TAG, "URI not set, handle: %p", io);
        return ESP_GMF_ERR_FAIL;
    }
    if (lfs2_io->is_open) {
        ESP_LOGE(TAG, "Already opened, p: %p", lfs2_io);
        return ESP_GMF_ERR_FAIL;
    }
    char *path = get_mount_path(uri);
    if (path == NULL) {
        ESP_LOGE(TAG, "Invalid URI (%s).", uri);
        return ESP_GMF_ERR_FAIL;
    }
    ESP_LOGI(TAG, "Open, dir:%d, uri:%s", ((lfs2_io_cfg_t *)lfs2_io->base.parent.cfg)->dir, uri);

    if (((lfs2_io_cfg_t *)lfs2_io->base.parent.cfg)->dir == ESP_GMF_IO_DIR_READER) {
        lfs2_io->file = fopen(path, "rb");
        if (lfs2_io->file == NULL) {
            ESP_LOGE(TAG, "Failed to open for read: %s, err: %s", path, strerror(errno));
            return ESP_GMF_ERR_FAIL;
        }
        struct stat sz = {0};
        stat(path, &sz);
        esp_gmf_io_set_size(io, (uint64_t)sz.st_size);
        esp_gmf_info_file_t info = {0};
        esp_gmf_io_get_info(io, &info);
        ESP_LOGI(TAG, "File size: %d byte, file position: %lld", (int)sz.st_size, info.pos);
        if (info.pos > 0) {
            if (fseek(lfs2_io->file, (long)info.pos, SEEK_SET) != 0) {
                ESP_LOGE(TAG, "Seek to %lld failed, err: %s", info.pos, strerror(errno));
                fclose(lfs2_io->file);
                lfs2_io->file = NULL;
                return ESP_GMF_ERR_FAIL;
            }
        }
    } else if (((lfs2_io_cfg_t *)lfs2_io->base.parent.cfg)->dir == ESP_GMF_IO_DIR_WRITER) {
        lfs2_io->file = fopen(path, "wb");
        if (lfs2_io->file == NULL) {
            ESP_LOGE(TAG, "Failed to open for write: %s, err: %s", path, strerror(errno));
            return ESP_GMF_ERR_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "The type must be reader or writer");
        return ESP_GMF_ERR_FAIL;
    }

    lfs2_io->is_open = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_io_t _lfs2_acquire_read(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;

    size_t rlen = fread(pload->buf, 1, wanted_size, lfs2_io->file);
    ESP_LOGD(TAG, "Read len: %zu-%lu", rlen, wanted_size);
    if (rlen == 0 && ferror(lfs2_io->file)) {
        ESP_LOGE(TAG, "The error is happened in reading data, error msg: %s", strerror(errno));
        return ESP_GMF_IO_FAIL;
    }
    pload->valid_size = rlen;
    if (rlen < wanted_size) {
        pload->is_done = true;
        ESP_LOGI(TAG, "No more data, ret: %zu", rlen);
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _lfs2_release_read(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(handle, &info);
    ESP_LOGD(TAG, "Update len = %d, pos = %d/%d", pload->valid_size, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos(handle, pload->valid_size);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _lfs2_acquire_write(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted_size, int block_ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t _lfs2_release_write(esp_gmf_io_handle_t handle, void *payload, int block_ticks)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)handle;
    esp_gmf_payload_t *pload = (esp_gmf_payload_t *)payload;

    size_t wlen = fwrite(pload->buf, 1, pload->valid_size, lfs2_io->file);
    if (wlen != pload->valid_size) {
        ESP_LOGE(TAG, "The error is happened in writing data, error msg: %s", strerror(errno));
        return ESP_GMF_IO_FAIL;
    }
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(handle, &info);
    ESP_LOGD(TAG, "Write len = %zu, pos = %d/%d", wlen, (int)info.pos, (int)info.size);
    esp_gmf_io_update_pos(handle, wlen);
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_t _lfs2_seek(esp_gmf_io_handle_t io, uint64_t seek_byte_pos)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(io, &info);
    ESP_LOGI(TAG, "Seek position, total_bytes: %lld, seek: %lld", info.size, seek_byte_pos);
    if (seek_byte_pos > info.size) {
        ESP_LOGE(TAG, "Seek position is out of range, total_bytes: %lld, seek: %lld", info.size, seek_byte_pos);
        return ESP_GMF_ERR_OUT_OF_RANGE;
    }
    if (fseek(lfs2_io->file, (long)seek_byte_pos, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "Error seek file, error message: %s, line: %d", strerror(errno), __LINE__);
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _lfs2_reset(esp_gmf_io_handle_t io)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)io;
    if (lfs2_io->file != NULL) {
        fseek(lfs2_io->file, 0, SEEK_SET);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _lfs2_close(esp_gmf_io_handle_t io)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)io;
    esp_gmf_info_file_t info = {0};
    esp_gmf_io_get_info(io, &info);
    ESP_LOGI(TAG, "Close, %p, pos = %d/%d", lfs2_io, (int)info.pos, (int)info.size);
    if (lfs2_io->is_open) {
        fclose(lfs2_io->file);
        lfs2_io->file = NULL;
        lfs2_io->is_open = false;
    }
    esp_gmf_io_set_pos(io, 0);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _lfs2_delete(esp_gmf_io_handle_t io)
{
    lfs2_io_stream_t *lfs2_io = (lfs2_io_stream_t *)io;
    ESP_LOGD(TAG, "Delete, %s-%p", OBJ_GET_TAG(lfs2_io), lfs2_io);
    void *cfg = OBJ_GET_CFG(io);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_io_deinit(io);
    esp_gmf_oal_free(lfs2_io);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_io_lfs2_init(lfs2_io_cfg_t *config, esp_gmf_io_handle_t *io)
{
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, io, return ESP_GMF_ERR_INVALID_ARG);
    *io = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    lfs2_io_stream_t *lfs2_io = esp_gmf_oal_calloc(1, sizeof(lfs2_io_stream_t));
    ESP_GMF_MEM_VERIFY(TAG, lfs2_io, return ESP_GMF_ERR_MEMORY_LACK,
                       "lfs2 io stream", sizeof(lfs2_io_stream_t));

    lfs2_io->base.dir = config->dir;
    lfs2_io->base.type = ESP_GMF_IO_TYPE_BYTE;

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)lfs2_io;
    obj->new_obj = _lfs2_new;
    obj->del_obj = _lfs2_delete;

    lfs2_io_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto _lfs2_fail;},
                       "lfs2_io cfg", sizeof(*config));
    memcpy(cfg, config, sizeof(*config));
    esp_gmf_obj_set_config(obj, cfg, sizeof(*config));

    ret = esp_gmf_obj_set_tag(obj, (config->name == NULL ? "io_lfs2" : config->name));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _lfs2_fail, "Failed to set obj tag");

    lfs2_io->base.open  = _lfs2_open;
    lfs2_io->base.close = _lfs2_close;
    lfs2_io->base.seek  = _lfs2_seek;
    lfs2_io->base.reset = _lfs2_reset;

    esp_gmf_io_init(obj, NULL);

    if (config->dir == ESP_GMF_IO_DIR_WRITER) {
        lfs2_io->base.acquire_write = _lfs2_acquire_write;
        lfs2_io->base.release_write = _lfs2_release_write;
    } else if (config->dir == ESP_GMF_IO_DIR_READER) {
        lfs2_io->base.acquire_read = _lfs2_acquire_read;
        lfs2_io->base.release_read = _lfs2_release_read;
    } else {
        ESP_LOGW(TAG, "Does not set read or write function");
        ret = ESP_GMF_ERR_NOT_SUPPORT;
        goto _lfs2_fail;
    }

    *io = obj;
    return ESP_GMF_ERR_OK;

_lfs2_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}
