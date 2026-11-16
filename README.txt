ESP32S3_BackView 
================

Turns an ESP32-S3 camera board into a Wi-Fi access point and shows the
camera image as MJPEG in a smartphone browser.
It is intended as a car back view (rear check) camera, so the image is
mirrored left-to-right, the same as a rear-view mirror.

(Japanese version: README_ja.txt)

■ note.com
------------
https://note.com/hiraide62/n/n8950254cad24?hl=en

■ Features
------------
- The ESP32-S3 acts as a Wi-Fi access point (no router needed)
- Open http://192.168.4.1/ in a smartphone browser to see the video
- Video: VGA (640x480), JPEG quality 12, up to 20fps
- Mirrored left-to-right (looks the same as a rear-view mirror)
- Up to 4 devices can join the Wi-Fi. Only 1 device can watch the video
  (from the 2nd device on, /stream returns 503 "使用中です" = "in use")


■ Requirements
----------------
- Board    : Freenove ESP32-S3 WROOM CAM
- Sensor   : OV3660
- Flash    : 8MB (DIO)
- PSRAM    : Octal 8MB, 40MHz
- CPU      : 160MHz
- SDK      : ESP-IDF v6.1 (installed with EIM), C++
- Component: espressif/esp32-camera (fetched automatically at build time)

Camera pins
  XCLK=15  SDA=4  SCL=5
  D7..D0=16,17,18,12,10,8,9,11
  VSYNC=6  HREF=7  PCLK=13  PWDN/RESET=-1


■ Usage
---------
1. Power on the ESP32-S3
2. Connect your smartphone to this Wi-Fi
     SSID     : VINTAGE_BACKVIEW
     Password : VINTAGEAMERICAN
3. Open http://192.168.4.1/ in the browser
     /        : HTML page showing the video (black background, full width,
                title "American BackView")
     /stream  : the MJPEG video itself

Note: Tested with Firefox on a smartphone.


■ Build and flash (Windows)
-----------------------------
Install ESP-IDF v6.1 with EIM.
The cmd files load C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
and then run idf.py. Edit them if your install path is different.

  build.cmd                     build
  burn.cmd [COM port]           flash
  flash_monitor.cmd [COM port]  flash and monitor
  monitor.cmd [COM port]        monitor

The COM port can be given as an argument (e.g. burn.cmd COM13).
The default port used when it is omitted is written in each cmd file.

To run directly in an ESP-IDF environment:
  idf.py build
  idf.py -p COM13 flash monitor


■ Files
---------
  CMakeLists.txt          project (name: ESP32S3_BackView)
  sdkconfig.defaults      only the settings changed from defaults (Flash, PSRAM, CPU)
  main/
    main.cpp              entry point. Creates and runs backViewEcst
    backViewEcst.cpp/.h   parent class. Creates the part classes below and passes "this"
    hiraWifiAp.cpp/.h     NVS init and Wi-Fi access point start (up to 4 devices)
    hiraCamera.cpp/.h     camera init and getting one JPEG frame (VGA, quality 12, mirrored)
    hiraHttpServer.cpp/.h HTTP server (port 80, own socket code, non-blocking, 1 task)
    idf_component.yml     uses esp32-camera
  build.cmd etc.          for build and flash
  README_ja.txt           Japanese version of this file


■ Settings
------------
  main/hiraWifiAp.cpp
    AP_SSID / AP_PASS     SSID and password
    AP_CHANNEL            Wi-Fi channel (default 1)
    AP_MAX_CONN           number of Wi-Fi devices (default 4)

  main/hiraCamera.cpp
    CAMERA_JPEG_QUALITY   JPEG quality (0-63, lower is better. default 12)
    CAMERA_HMIRROR        left-right mirror (on this board's OV3660, 0 gives the mirrored image)
    CAMERA_VFLIP          up-down flip (1 to flip)

  main/hiraHttpServer.cpp
    HTTP_FRAME_INTERVAL_MS  frame interval (default 50ms = 20fps)

  main/hiraHttpServer.h
    MAX_CONN              number of HTTP connections held at once (default 4)
    MAX_STREAM            number of video viewers (default 1)
                          Increase this to let more devices watch.
                          Each frame is sent to all viewers before it is
                          returned to the camera.


■ How it works
----------------
- The HTTP server uses select (waits up to 10ms) and non-blocking sockets,
  and handles all connections in a single task.
- /stream sends JPEG frames one after another as multipart/x-mixed-replace.
- A connection is closed if its request is not complete within 5 seconds,
  or if not a single byte can be sent for 5 seconds.
- If the camera is not available, /stream returns 503
  "Camera not available".
- No power saving (it always runs as an access point).
- Logs are output with printf only.
