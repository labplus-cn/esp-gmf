/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"
#include "dev_display_lcd.h"
#include "esp_lcd_panel_ops.h"
#if CONFIG_MPYTHON_V3_BOARD
#include "logo_mpython_v3_320x172_lcd.h"
#elif CONFIG_LABPLUS_LEDONG_V2_BOARD
#include "logo_labplus_ledong_v2_320x172_lcd.h"
#elif CONFIG_LABPLUS_XUNFEI_JS_PRIMARY_BOARD || CONFIG_LABPLUS_XUNFEI_JS_MIDDLE_BOARD
#include "logo_xunfei_320x172_lcd.h"
#endif
#include "driver/gpio.h"

static const char *TAG = "DEV_DISPLAY_LCD";

bool lcd_dma_complete_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *) user_ctx;

    // 此处可在IRAM中快速处理，避免临界区
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // ESP_EARLY_LOGE("tag", "in isr");

    // 通知LVGL：这一帧刷完了，可以画下一帧了
    if (lcd_handles->transfer_done_cb != NULL){
        lcd_handles->transfer_done_cb(lcd_handles->transfer_done_user_data);
    }
   
    // isr_cnt++;
    // finish = true;
    // // 或者释放信号量，唤醒绘制任务
    xSemaphoreGiveFromISR(lcd_handles->dma_finish_sem, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE) {
        // portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    return true;
}

int dev_display_lcd_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_display_lcd_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }
    esp_err_t ret = ESP_FAIL;
    dev_display_lcd_handles_t *handle = NULL;

    const dev_display_lcd_config_t *config = (const dev_display_lcd_config_t *)cfg;
    ESP_LOGI(TAG, "Initializing LCD display: %s, chip: %s, sub_type: %s",
             config->name, config->chip, config->sub_type);
    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        return -1;
    }
    if (entry_desc->init_func == NULL) { 
        ESP_LOGE(TAG, "LCD sub_type '%s' has no init function", config->sub_type);
        return -1;
    }
    ret = entry_desc->init_func((void *)config, cfg_size, (void **)&handle); //调用 dev_display_lcd_sub_spi_init()函数
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sub device: %s", config->sub_type);
        return -1;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lcd_dma_complete_callback,
    };
    /* Register done callback */
    ret = esp_lcd_panel_io_register_event_callbacks(handle->io_handle, &cbs, handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to regist panel io event callback: %s", esp_err_to_name(ret));
    }

    // Reset LCD panel if needed
    if (config->need_reset) {
        ret = esp_lcd_panel_reset(handle->panel_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
            entry_desc->deinit_func(handle);
            return -1;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    // Initialize LCD panel
    ret = esp_lcd_panel_init(handle->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        entry_desc->deinit_func(handle);
        return -1;
    }

    ret = esp_lcd_panel_invert_color(handle->panel_handle, config->invert_color );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to invert LCD panel: %s", esp_err_to_name(ret));
    }

    ret = esp_lcd_panel_mirror(handle->panel_handle, config->mirror_x, config->mirror_y);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mirror LCD panel: %s", esp_err_to_name(ret));
    }

    ret = esp_lcd_panel_swap_xy(handle->panel_handle, config->swap_xy);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to swap LCD panel XY: %s", esp_err_to_name(ret));
    }

    // Invert color if needed
    if (config->invert_color) {
        ret = esp_lcd_panel_invert_color(handle->panel_handle, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to invert color on LCD panel: %s", esp_err_to_name(ret));
        }
    }

    ret = esp_lcd_panel_set_gap(handle->panel_handle, config->gap_x, config->gap_y);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set LCD panel gap: %s", esp_err_to_name(ret));
    }
    
    if (BOARD_LCD_BL >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << BOARD_LCD_BL,
        };
        gpio_config(&io_conf);
        gpio_set_level(BOARD_LCD_BL, 1);
    }

    // Turn on display
    esp_lcd_panel_disp_on_off(handle->panel_handle, true);
    ESP_LOGI(TAG, "Successfully initialized LCD display: %s (sub_type: %s), panel: %p, io: %p",
             config->name, config->sub_type, handle->panel_handle, handle->io_handle);
    *device_handle = handle;

    handle->dma_finish_sem = xSemaphoreCreateBinary();
    
    // handle->lcd_buf = (uint16_t *)heap_caps_aligned_alloc(32, AREA_BYTES,   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    handle->lcd_buf = (uint16_t *)heap_caps_aligned_alloc(32, config->lcd_width * config->lcd_height *2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);  // MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(handle != NULL);
    // memset(handle->lcd_buf, 0, AREA_BYTES);
    memset(handle->lcd_buf, 0, config->lcd_width * config->lcd_height * 2);

    lcd_set_color(handle, GUI_Black);
    return 0;
}

int dev_display_lcd_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    free(lcd_handles->lcd_buf);

    if (cfg) {
        const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(cfg->sub_type);
        if (desc && desc->deinit_func) {
            int ret = desc->deinit_func(device_handle);
            if (ret != 0) {
                ESP_LOGE(TAG, "LCD(sub_type: %s) deinit failed with error: %d", cfg->sub_type, ret);
            } else {
                ESP_LOGI(TAG, "LCD(sub_type: %s) deinitialized successfully", cfg->sub_type);
            }
        } else {
            ESP_LOGW(TAG, "No custom deinit function found for sub type '%s'", cfg->sub_type);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get config from device handle");
    }
    return 0;
}

void lcd_draw_logo(void *device_handle)
{
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    assert(lcd_handles != NULL);
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    memcpy(lcd_handles->lcd_buf, logo_en_320x172_lcd, cfg->lcd_width* cfg->lcd_height* sizeof(uint16_t));
    xSemaphoreTake(lcd_handles->dma_finish_sem, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(lcd_handles->panel_handle, 0, 0, cfg->lcd_width, cfg->lcd_height, (uint16_t *)lcd_handles->lcd_buf);
}

void lcd_set_color(void *device_handle, int color)
{
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    assert(lcd_handles != NULL);
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(lcd_handles, (void **)&cfg);
    uint16_t *buffer = (uint16_t *)malloc(cfg->lcd_width * sizeof(uint16_t));
    if (NULL == buffer){
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
    }
    else{
        for (size_t i = 0; i < cfg->lcd_width; i++){
            buffer[i] = color;
        }

        for (int y = 0; y < cfg->lcd_height; y++){
            // xSemaphoreTake(lcd_handles->dma_finish_sem, portMAX_DELAY);
            esp_lcd_panel_draw_bitmap(lcd_handles->panel_handle, 0, y, cfg->lcd_width, y+1, buffer);
        }

        free(buffer);
    }
}

void lcd_draw_image(void *device_handle, int x, int y, int width, int height, const void *buff)
{
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    assert(lcd_handles != NULL);
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    xSemaphoreTake(lcd_handles->dma_finish_sem, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(lcd_handles->panel_handle, x, y, (width > cfg->lcd_width)? cfg->lcd_width : width, (height > cfg->lcd_height)? cfg->lcd_height : height, (uint16_t *)buff);
}

void lcd_flush(void *device_handle, const void *buff)
{
    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)device_handle;
    assert(lcd_handles != NULL);
    dev_display_lcd_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);

    uint16_t * buf = (uint16_t *)buff;
    uint16_t * buf_tmp = (uint16_t *)lcd_handles->lcd_buf;
    // uint32_t t1 = esp_timer_get_time();
    // uint32_t y_off = 0;
    // for(uint8_t j = 0; j < AREA_NUMS; j++){
    //     xSemaphoreTake(lcd_handles->dma_finish_sem, portMAX_DELAY);
    //     for(uint32_t i = 0; i < AREA_WORD; i++){
    //         buf_tmp[i] = __builtin_bswap16(buf[y_off + i]);
    //     }
    //     esp_lcd_panel_draw_bitmap(lcd_handles->panel, 0, AREA_LINES*j, BOARD_LCD_H_RES, AREA_LINES*(j+1), buf_tmp);
    //     y_off += AREA_WORD;
    // }

    uint32_t cnt = cfg->lcd_width * cfg->lcd_height;
    for(uint32_t i =0; i < cnt; i++ )
        buf_tmp[i] = __builtin_bswap16(buf[i]);
    xSemaphoreTake(lcd_handles->dma_finish_sem, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(lcd_handles->panel_handle, 0, 0, cfg->lcd_width, cfg->lcd_height, buf_tmp);

    // ESP_LOGE("modlcd", "%ld\n", (uint32_t)esp_timer_get_time() - t1);  
}