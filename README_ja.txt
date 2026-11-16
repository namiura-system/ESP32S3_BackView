ESP32S3_BackView
================


ESP32-S3 のカメラボードを Wi-Fi アクセスポイントにして、
スマートフォンのブラウザにカメラの映像を MJPEG で表示するプログラムです。
車のバックビュー（後方確認）用に、映像はルームミラーと同じ左右反転で表示します。


■ できること
--------------
・ESP32-S3 が Wi-Fi アクセスポイントになる（ルーター不要）
・スマートフォンのブラウザで http://192.168.4.1/ を開くと映像が出る
・映像は VGA（640x480）、JPEG 画質 12、最大 20fps
・映像は左右反転（ルームミラーと同じ見え方）
・Wi-Fi は 4 台までつなげる。映像を見られるのは 1 台だけ
  （2 台目以降の /stream には 503「使用中です」を返す）


■ 動作環境
------------
・ボード   : Freenove ESP32-S3 WROOM CAM
・センサー : OV3660
・Flash    : 8MB（DIO）
・PSRAM    : Octal 8MB、40MHz
・CPU      : 160MHz
・開発環境 : ESP-IDF v6.1（EIM でインストール）、C++
・使う部品 : espressif/esp32-camera（ビルド時に自動で取得）

カメラのピン配置
  XCLK=15  SDA=4  SCL=5
  D7..D0=16,17,18,12,10,8,9,11
  VSYNC=6  HREF=7  PCLK=13  PWDN/RESET=-1


■ 使い方
----------
1. ESP32-S3 に電源を入れる
2. スマートフォンの Wi-Fi で次につなぐ
     SSID     : VINTAGE_BACKVIEW
     パスワード : VINTAGEAMERICAN
3. ブラウザで http://192.168.4.1/ を開く
     /        : 映像を表示する HTML（黒背景、幅いっぱい、タイトル "American BackView"）
     /stream  : MJPEG の映像そのもの

※ スマートフォンの Firefox で表示を確認しています。


■ ビルドと書き込み（Windows）
------------------------------
ESP-IDF v6.1 を EIM でインストールしておきます。
cmd ファイルは C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
を読み込んでから idf.py を実行します。インストール先が違うときは書き換えてください。

  build.cmd                  ビルド
  burn.cmd [COMポート]        書き込み
  flash_monitor.cmd [COMポート] 書き込みしてモニター
  monitor.cmd [COMポート]     モニター

COM ポートは引数で指定できます（例: burn.cmd COM13）。
省略したときの既定値は、各 cmd ファイルの中に書いてあります。

ESP-IDF の環境で直接実行する場合:
  idf.py build
  idf.py -p COM13 flash monitor


■ ファイル構成
----------------
  CMakeLists.txt          プロジェクト（名前は ESP32S3_BackView）
  sdkconfig.defaults      既定値から変える設定だけ（Flash・PSRAM・CPU）
  main/
    main.cpp              入口。backViewEcst を作って動かす
    backViewEcst.cpp/.h   親クラス。下の部品クラスを作り、this を渡す
    hiraWifiAp.cpp/.h     NVS の初期化と Wi-Fi アクセスポイントの起動（4 台まで）
    hiraCamera.cpp/.h     カメラの初期化と JPEG 1 枚の取得（VGA・画質 12・左右反転）
    hiraHttpServer.cpp/.h HTTP サーバー（80 番、ソケットで自作、ノンブロッキング、1 タスク）
    idf_component.yml     esp32-camera を使う指定
  build.cmd ほか          ビルド・書き込み用


■ 設定を変えたいとき
----------------------
  main/hiraWifiAp.cpp
    AP_SSID / AP_PASS     SSID とパスワード
    AP_CHANNEL            Wi-Fi のチャンネル（既定 1）
    AP_MAX_CONN           Wi-Fi でつなげる台数（既定 4）

  main/hiraCamera.cpp
    CAMERA_JPEG_QUALITY   JPEG 画質（0-63、小さいほど高画質。既定 12）
    CAMERA_HMIRROR        左右反転（このボードの OV3660 は 0 で左右反転になる）
    CAMERA_VFLIP          上下反転（1 で反転）

  main/hiraHttpServer.cpp
    HTTP_FRAME_INTERVAL_MS  1 コマの間隔（既定 50ms = 20fps）

  main/hiraHttpServer.h
    MAX_CONN              同時に持てる HTTP 接続の数（既定 4）
    MAX_STREAM            映像を送る相手の数（既定 1）
                          複数台で見たいときはここを増やす。
                          1 枚の画像を全員に送り終えてからカメラに返す作りです。


■ しくみ
----------
・HTTP サーバーは select（最大 10ms 待ち）とノンブロッキングソケットで、
  1 つのタスクだけで全部の接続を受け持ちます。
・/stream は multipart/x-mixed-replace で JPEG を次々に送ります。
・要求が 5 秒そろわない、または 5 秒 1 バイトも送れない接続は閉じます。
・カメラが使えないときの /stream は 503「Camera not available」を返します。
・省電力は入れていません（アクセスポイントとして常に動かします）。
・ログは printf だけで出しています。
