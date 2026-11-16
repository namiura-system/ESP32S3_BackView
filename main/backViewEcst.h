#pragma once

class hiraWifiAp;
class hiraCamera;
class hiraHttpServer;

// Parent of the whole back view. Creates the parts and runs them in order
// バックビュー全体の親。部品を作って順に動かす
class backViewEcst {
public:
    backViewEcst();
    ~backViewEcst();

    // Starts the AP, initializes the camera, and starts the HTTP task. Does not return on success (returns false on failure)
    // AP 起動、カメラ初期化、HTTP タスク起動まで進める。成功したら戻らない（失敗したら false）
    bool go();

    hiraWifiAp *getHWA() { return HWA; }
    hiraCamera *getHCA() { return HCA; }
    hiraHttpServer *getHHS() { return HHS; }

private:
    // NVS init and Wi-Fi access point start
    hiraWifiAp *HWA;       // NVS の初期化と、Wi-Fi アクセスポイントの起動
    // Camera
    hiraCamera *HCA;       // カメラ
    // HTTP server (HTML and MJPEG)
    hiraHttpServer *HHS;   // HTTP サーバー（HTML と MJPEG）
};
