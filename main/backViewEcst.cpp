#include "backViewEcst.h"

#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hiraCamera.h"
#include "hiraHttpServer.h"
#include "hiraWifiAp.h"

backViewEcst::backViewEcst()
{
    // Passing "this" to each part means passing the parent
    // それぞれ this を渡すのは parent を渡すという意味
    HWA = new hiraWifiAp(this);
    HCA = new hiraCamera(this);
    HHS = new hiraHttpServer(this);
}

backViewEcst::~backViewEcst()
{
    delete HHS;
    delete HCA;
    delete HWA;
}

bool backViewEcst::go()
{
    // Returns false if the AP could not be started
    if (!HWA->go()) return false;          // AP を起動できなかったら false
    std::printf("BVE: wifi ap ready\n");
    // Does not stop on failure (/stream returns 503)
    HCA->init("FRAMESIZE_VGA");            // 失敗しても止めない（/stream に 503 を返す）
    // Starts the HTTP task. Returns false on failure
    if (!HHS->GoThread()) return false;    // HTTP タスクを起動。失敗したら false
    std::printf("BVE: http thread started\n");

    // Keep waiting without returning. This object lives on the app_main stack,
    // so returning would destroy it and the HTTP task would use a destroyed object
    // 戻らずに待ち続ける。このオブジェクトは app_main のスタックにあるので、
    // 戻ると消えてしまい、HTTP タスクが消えたオブジェクトを使うことになる
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
