#pragma once

#include <cstddef>
#include <cstdint>

class backViewEcst;

// HTTP server. Port 80, runs non-blocking sockets in a single task
//   GET /        -> HTML (<img src="/stream">)
//   GET /stream  -> MJPEG (multipart/x-mixed-replace). 503 if all slots are in use
// HTTP サーバー。80番ポート、ノンブロッキングのソケットを1つのタスクで回す
//   GET /        → HTML（<img src="/stream">）
//   GET /stream  → MJPEG（multipart/x-mixed-replace）。枠が一杯なら 503
class hiraHttpServer {
public:
    // Number of connections held at once
    static constexpr int MAX_CONN = 4;     // 同時に持てる接続の数
    // Number of video viewers (change this for more devices)
    static constexpr int MAX_STREAM = 1;   // 映像を送る相手の数（複数台にするときはここを変える）

    explicit hiraHttpServer(backViewEcst *parent);
    ~hiraHttpServer();

    // Creates the listening socket (non-blocking, port 80) and starts the HTTP task. Returns false on failure
    // 待ち受けソケットを作り（ノンブロッキング、80番）、HTTP タスクを起動する。失敗したら false
    bool GoThread();

private:
    enum State {
        // Free
        ST_FREE,          // 空き
        // Reading the request
        ST_READ,          // 要求を読んでいる
        // Sending a reply (closed when done)
        ST_REPLY,         // 返事を送っている（送り終えたら閉じる）
        // Sending the MJPEG header
        ST_STREAM_HEAD,   // MJPEG のヘッダを送っている
        // Waiting for the next frame
        ST_STREAM_WAIT,   // 次のフレームを待っている
        // Sending a frame
        ST_STREAM_SEND,   // フレームを送っている
    };

    // A piece of data to send. Sent in order without joining (so the JPEG is not copied)
    // 送るデータのかけら。つなげずに順に送る（JPEG をコピーしないため）
    struct Piece {
        const uint8_t *p;
        size_t n;
    };

    struct Conn {
        // -1 means free
        int sock;              // -1 で空き
        State state;
        // Received request
        char req[512];         // 受け取った要求
        size_t req_len;
        // Reply header, frame header
        char head[256];        // 返事のヘッダ、フレームの頭
        Piece pieces[3];
        int piece_count;
        // Piece being sent
        int piece_idx;         // 送っているかけら
        // Position within the piece
        size_t piece_pos;      // かけらの中の位置
        // Time of the last progress (for timeout)
        int64_t last_ms;       // 最後に進んだ時刻（タイムアウト用）
    };

    static void task(void *arg);
    void loop();
    void acceptNew(int64_t now);
    void readConn(Conn &c, int64_t now);
    void handleRequest(Conn &c, int64_t now);
    void reply(Conn &c, const char *status, const char *type, const char *body, int64_t now);
    void writeConn(Conn &c, int64_t now);
    // 1: all sent  0: in progress  -1: closed
    int sendPieces(Conn &c, int64_t now);   // 1:送り終えた 0:途中 -1:閉じた
    void startFrame(int64_t now);
    void endFrameIfDone();
    void checkTimeout(Conn &c, int64_t now);
    void closeConn(Conn &c, const char *why);
    int countStream() const;
    bool isSending(const Conn &c) const;

    backViewEcst *parent;
    int listen_sock;
    Conn conns[MAX_CONN];
    // A frame is borrowed from the camera
    bool frame_active;          // カメラからフレームを借りている
    const uint8_t *frame_data;
    size_t frame_len;
    // Time of the last capture (for the frame rate limit)
    int64_t last_frame_ms;      // 最後に撮った時刻（コマ数の上限用）
    // Whether the last capture succeeded (so the log is not repeated)
    bool capture_ok;            // 前回の撮影が成功したか（ログを繰り返さないため）
};
