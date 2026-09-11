#include "ceko.h"
#include <math.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "lvgl.h"

static SemaphoreHandle_t flush_done;
static esp_lcd_panel_handle_t panel;
static lv_obj_t *eyes[2], *mouth, *label, *dot;
static uint32_t blink_at = 3000, blink_start;
static bool blinking;
static float mouth_level;
// Same CO5300 initialization used by Waveshare with esp_lcd_sh8601.
static const sh8601_lcd_init_cmd_t cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0}, {0x51, (uint8_t[]){0xB0}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0,6,1,0xD7}, 4, 0}, {0x2B, (uint8_t[]){0,0,1,0xD1}, 4, 0},
    {0x11, (uint8_t[]){0}, 0, 100}, {0x29, (uint8_t[]){0}, 0, 0},
};
static bool transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(flush_done, &wake);
    return wake == pdTRUE;
}
static void flush(lv_display_t *disp, const lv_area_t *a, uint8_t *pixels) {
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_width(a)*lv_area_get_height(a));
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, a->x1+6, a->y1, a->x2+7, a->y2+1, pixels));
}
static void flush_wait(lv_display_t *disp) { xSemaphoreTake(flush_done, portMAX_DELAY); }
static void round_area(lv_event_t *e) {
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1; a->y1 &= ~1; a->x2 |= 1; a->y2 |= 1;
}
static uint32_t tick(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static lv_obj_t *shape(lv_obj_t *parent, int w, int h, int x, int y, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h); lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    return obj;
}
static void animate(lv_timer_t *timer) {
    uint32_t now = tick();
    ceko_state_t state = ceko_state_get();
    if (!blinking && (int32_t)(now-blink_at) >= 0) { blinking=true; blink_start=now; }
    float open = 1;
    if (blinking) {
        uint32_t age = now-blink_start;
        if (age < 90) open = 1-age/90.0f;
        else if (age < 140) open = 0;
        else if (age < 260) open = (age-140)/120.0f;
        else { blinking=false; blink_at=now+2600+esp_random()%4000; }
    }
    int eh = state == CEKO_THINK ? 66 : state == CEKO_LISTEN ? 116 : 100;
    int h = 8+(int)((eh-8)*open);
    int gaze_x = (int)(sinf(now/2700.0f)*7);
    int gaze_y = (int)(sinf(now/1900.0f)*4);
    uint32_t col = state == CEKO_ERROR ? 0xFF9B75 : state == CEKO_LISTEN ? 0x91FFCC : 0x75E5FF;
    for (int i=0; i<2; ++i) {
        lv_obj_set_size(eyes[i], 82, h);
        lv_obj_set_pos(eyes[i], 115+i*154+gaze_x, 200-h/2+gaze_y);
        lv_obj_set_style_bg_color(eyes[i], lv_color_hex(col), 0);
    }
    float target = state == CEKO_SPEAK ? ceko_level_get()/100.0f : 0;
    mouth_level += (target-mouth_level)*(target > mouth_level ? 0.65f : 0.35f);
    int mh = 8+(int)(mouth_level*62);
    int mw = 62+(int)(mouth_level*26);
    lv_obj_set_size(mouth, mw, mh);
    lv_obj_set_pos(mouth, 233-mw/2, 296-mh/2);
    lv_obj_set_style_bg_color(mouth, lv_color_hex(col), 0);
    char text[80]; ceko_status_get(text, sizeof text);
    if (state == CEKO_IDLE && !ceko_wifi_ready()) {
#ifndef CONFIG_CEKO_FACE_DEMO
        strcpy(text, "Wi-Fi bekleniyor");
#endif
    }
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 140);
    int opa = (state == CEKO_THINK || state == CEKO_BOOT) ? 90+(int)(80*(1+sinf(now/250.0f))) : 180;
    lv_obj_set_style_bg_opa(dot, opa, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
}
static void ui_task(void *arg) {
    // All LVGL operations live on this task; other tasks publish atomic values.
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    eyes[0]=shape(screen,82,100,115,150,0x75E5FF);
    eyes[1]=shape(screen,82,100,269,150,0x75E5FF);
    mouth=shape(screen,62,8,202,292,0x75E5FF);
    dot=shape(screen,8,8,229,96,0x75E5FF);
    label=lv_label_create(screen);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_color(label,lv_color_hex(0x637785),0);
    lv_obj_set_width(label,340);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    lv_timer_create(animate,33,NULL);
    for (;;) { uint32_t ms=lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(ms<5?5:ms>33?33:ms)); }
}
void face_start(void) {
    flush_done=xSemaphoreCreateBinary(); assert(flush_done);
    spi_bus_config_t bus={ .sclk_io_num=11,.data0_io_num=12,.data1_io_num=13,.data2_io_num=14,.data3_io_num=15,
        .max_transfer_sz=466*32*2 };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io={ .cs_gpio_num=10,.dc_gpio_num=-1,.spi_mode=0,.pclk_hz=40000000,
        .trans_queue_depth=2,.on_color_trans_done=transfer_done,.lcd_cmd_bits=32,.lcd_param_bits=8,.flags.quad_mode=true };
    esp_lcd_panel_io_handle_t handle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&handle));
    sh8601_vendor_config_t vendor={ .init_cmds=cmds,.init_cmds_size=sizeof cmds/sizeof cmds[0],.flags.use_qspi_interface=true };
    esp_lcd_panel_dev_config_t dev={ .reset_gpio_num=8,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel=16,.vendor_config=&vendor };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(handle,&dev,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel)); ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
#ifdef CONFIG_CEKO_DISPLAY_ROTATE_180
    uint8_t rotation=0xC0;
#else
    uint8_t rotation=0;
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(handle,0x02003600,&rotation,1));
    lv_init(); lv_tick_set_cb(tick);
    lv_display_t *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    void *buf=heap_caps_malloc(466*32*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL); assert(buf);
    lv_display_set_buffers(display,buf,NULL,466*32*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush); lv_display_set_flush_wait_cb(display,flush_wait);
    lv_display_add_event_cb(display,round_area,LV_EVENT_INVALIDATE_AREA,NULL);
    assert(xTaskCreate(ui_task,"face",6144,NULL,2,NULL)==pdPASS);
}
