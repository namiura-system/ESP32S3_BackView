#pragma once

#include <cstddef>
#include <cstdint>

class backViewEcst;

// Camera (handles only sensors that can output JPEG)
// カメラ（JPEG を出せるセンサーだけ扱う）
class hiraCamera {
public:
    explicit hiraCamera(backViewEcst *parent);
    ~hiraCamera();

    // Initializes with a size such as "FRAMESIZE_VGA" (returns false on failure, reason in getError())
    // "FRAMESIZE_VGA" などのサイズで初期化する（失敗したら false、getError() に理由）
    bool init(const char *frame_size);

    // Takes one frame. data is valid until release() is called
    // 1枚撮る。data は release() を呼ぶまで有効
    bool capture(const uint8_t **data, size_t *len);
    void release();

    bool isReady() const { return ready; }
    // Width of the last captured frame
    int getWidth() const { return width; }     // 最後に撮ったフレームの幅
    int getHeight() const { return height; }
    const char *getError() const { return error; }

private:
    bool start(int size);

    backViewEcst *parent;
    bool ready;
    int frame_size;   // framesize_t
    int width;
    int height;
    // camera_fb_t (returned with release())
    void *fb;         // camera_fb_t（release() で返す）
    const char *error;
};
