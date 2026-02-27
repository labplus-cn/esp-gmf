/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_lfs2.h"
#include "gmf_loader_setup_defaults.h"

static const char *TAG = "REC_LFS2";

#define DEFAULT_RECORD_SAMPLE_RATE  16000
#define DEFAULT_RECORD_CHANNEL      1
#define DEFAULT_RECORD_BITS         16

#define LFS2_PARTITION_LABEL  "vfs"
#define LFS2_BASE_PATH        "/littlefs"
#define RECORD_FILE_PATH      "/littlefs/rec001.aac"

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return ESP_GMF_ERR_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    int ret = 0;

    ESP_LOGI(TAG, "[ 1 ] Init codec device");
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.record_info.sample_rate    = DEFAULT_RECORD_SAMPLE_RATE;
    codec_info.record_info.bits_per_sample = DEFAULT_RECORD_BITS;
    codec_info.record_info.channel        = DEFAULT_RECORD_CHANNEL;
    codec_info.play_info = codec_info.record_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    esp_codec_dev_set_in_gain((esp_codec_dev_handle_t)esp_gmf_app_get_record_handle(), 32);

    ESP_LOGI(TAG, "[ 2 ] Mount LittleFS and init IO");
    esp_vfs_littlefs_conf_t lfs_conf = {
        .base_path              = LFS2_BASE_PATH,
        .partition_label        = LFS2_PARTITION_LABEL,
        .format_if_mount_failed = false,
        .dont_mount             = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&lfs_conf));

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);

    /* Register lfs2 writer into pool */
    lfs2_io_cfg_t lfs2_cfg = {
        .dir  = ESP_GMF_IO_DIR_WRITER,
        .name = "io_lfs2",
    };
    esp_gmf_io_handle_t lfs2_io = NULL;
    ret = esp_gmf_io_lfs2_init(&lfs2_cfg, &lfs2_io);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return; }, "Failed to init lfs2 IO");
    esp_gmf_pool_register_io(pool, lfs2_io, NULL);

    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_lfs2", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return; }, "Failed to new pipeline");

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), esp_gmf_app_get_record_handle());

    ESP_LOGI(TAG, "[ 3.1 ] Set output file path");
    esp_gmf_pipeline_set_out_uri(pipe, RECORD_FILE_PATH);

    ESP_LOGI(TAG, "[ 3.2 ] Reconfig audio encoder and report sound info");
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);
    esp_gmf_info_sound_t info = {
        .sample_rates = DEFAULT_RECORD_SAMPLE_RATE,
        .channels     = DEFAULT_RECORD_CHANNEL,
        .bits         = DEFAULT_RECORD_BITS,
        .format_id    = ESP_AUDIO_TYPE_AAC,
    };
    esp_gmf_audio_enc_reconfig_by_sound_info(enc_el, &info);
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    ESP_LOGI(TAG, "[ 3.3 ] Create task and bind to pipeline");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb  = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return; }, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    ESP_LOGI(TAG, "[ 4 ] Start pipeline, recording to %s", RECORD_FILE_PATH);
    esp_gmf_pipeline_run(pipe);

    ESP_LOGI(TAG, "[ 5 ] Wait 10 s then stop");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 6 ] Destroy resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_gmf_app_teardown_codec_dev();
}
