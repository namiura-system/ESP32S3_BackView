#pragma once

#include <cstdint>

class backViewEcst;

// NVS init and Wi-Fi access point start
// NVS の初期化と、Wi-Fi アクセスポイントの起動
class hiraWifiAp {
public:
    explicit hiraWifiAp(backViewEcst *parent);
    ~hiraWifiAp();

    // NVS init -> AP start (192.168.4.1). Returns false on failure
    // NVS 初期化 → AP 起動（192.168.4.1）。失敗したら false
    bool go();

private:
    // Logs Wi-Fi events (a phone connected / disconnected)
    // Wi-Fi のイベント（スマホがつながった・切れた）をログに出す
    static void onEvent(void *arg, const char *base, int32_t id, void *data);

    backViewEcst *parent;
};
