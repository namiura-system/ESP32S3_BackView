#include "hiraCamera.h"

#include <cstdio>
#include <cstring>

#include "esp_camera.h"

// 0-63, lower is better quality
#define CAMERA_JPEG_QUALITY  12               // 0-63、小さいほど高画質
// Clock sent to the sensor
#define CAMERA_XCLK          20000000         // センサーに送るクロック
// Frames thrown away right after init, until the exposure settles
#define CAMERA_SKIP_FRAMES   3                // 初期化直後、露出が落ち着くまで捨てるフレーム数
// On this board's OV3660, 0 gives a left-right mirrored image (looks the same as a rear-view mirror)
#define CAMERA_HMIRROR       0                // このボードのOV3660は0で左右反転（ルームミラーと同じ見え方）
// 1 flips up-down
#define CAMERA_VFLIP         0                // 1で上下反転

struct SizeName {
    const char *name;
    framesize_t size;
};

// Sizes that can be given. If larger than the sensor's maximum, it falls back to the sensor's maximum
// 指定できるサイズ。センサーの最大を超えたら、センサーの最大に下がる
static const SizeName SIZES[] = {
    {"FRAMESIZE_QVGA", FRAMESIZE_QVGA},   // 320x240
    {"FRAMESIZE_VGA",  FRAMESIZE_VGA},    // 640x480
    {"FRAMESIZE_UXGA", FRAMESIZE_UXGA},   // 1600x1200
};

static bool find_size(const char *name, framesize_t *size)
{
    if (!name) return false;
    for (const auto &s : SIZES) {
        if (std::strcmp(s.name, name) == 0) {
            *size = s.size;
            return true;
        }
    }
    return false;
}

hiraCamera::hiraCamera(backViewEcst *p)
    : parent(p), ready(false), frame_size(FRAMESIZE_VGA), width(0), height(0), fb(nullptr), error("")
{
}

hiraCamera::~hiraCamera()
{
    release();
    if (ready) esp_camera_deinit();
}

bool hiraCamera::init(const char *name)
{
    framesize_t size;
    if (!find_size(name, &size)) {
        std::printf("HCA: unknown frame size \"%s\"\n", name ? name : "");
        error = "unknown frame size";
        return false;
    }
    return start(size);
}

// Initializes for JPEG output with the Freenove ESP32-S3 WROOM CAM pin layout
// Freenove ESP32-S3 WROOM CAM のピン配置で、JPEG 出力として初期化する
bool hiraCamera::start(int size)
{
    camera_config_t cfg = {};
    cfg.pin_pwdn = -1;
    cfg.pin_reset = -1;
    cfg.pin_xclk = 15;
    cfg.pin_sccb_sda = 4;
    cfg.pin_sccb_scl = 5;
    cfg.pin_d7 = 16;
    cfg.pin_d6 = 17;
    cfg.pin_d5 = 18;
    cfg.pin_d4 = 12;
    cfg.pin_d3 = 10;
    cfg.pin_d2 = 8;
    cfg.pin_d1 = 9;
    cfg.pin_d0 = 11;
    cfg.pin_vsync = 6;
    cfg.pin_href = 7;
    cfg.pin_pclk = 13;
    cfg.xclk_freq_hz = CAMERA_XCLK;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = static_cast<framesize_t>(size);
    cfg.jpeg_quality = CAMERA_JPEG_QUALITY;
    // 2 buffers + take the latest: capture the next frame while sending
    // バッファ2つ＋最新を取る：送っている間に次のフレームを取り込んでおく
    cfg.fb_count = 2;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        std::printf("HCA: init failed (%s)\n", esp_err_to_name(err));
        error = "camera not ready";
        ready = false;
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    camera_sensor_info_t *info = s ? esp_camera_sensor_get_info(&s->id) : nullptr;
    if (s) {
        s->set_hmirror(s, CAMERA_HMIRROR);
        s->set_vflip(s, CAMERA_VFLIP);
    }

    // Right after start the exposure is not right, so throw some frames away. The last one tells the real size
    // 起動直後は露出が合っていないので、何枚か捨てる。最後の1枚で実際のサイズを知る
    for (int i = 0; i < CAMERA_SKIP_FRAMES; i++) {
        camera_fb_t *f = esp_camera_fb_get();
        if (f) {
            width = f->width;
            height = f->height;
            esp_camera_fb_return(f);
        }
    }
    frame_size = size;
    ready = true;
    std::printf("HCA: %s ready (%dx%d)\n", info ? info->name : "sensor", width, height);
    return true;
}

bool hiraCamera::capture(const uint8_t **data, size_t *len)
{
    if (!ready) {
        error = "camera not ready";
        return false;
    }
    release();
    camera_fb_t *f = esp_camera_fb_get();
    if (!f) {
        error = "capture failed";
        return false;
    }
    fb = f;
    width = f->width;
    height = f->height;
    *data = f->buf;
    *len = f->len;
    return true;
}

void hiraCamera::release()
{
    if (fb) {
        esp_camera_fb_return(static_cast<camera_fb_t *>(fb));
        fb = nullptr;
    }
}
