#include "hiraHttpServer.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "backViewEcst.h"
#include "hiraCamera.h"

#define HTTP_PORT               80
#define HTTP_BACKLOG            4
#define HTTP_FRAME_INTERVAL_MS  50      // 20fps
// Time to wait when nothing happens (select limit)
#define HTTP_WAIT_MS            10      // 何も起きないときに待つ時間（select の上限）
// Close if the request is not complete after this time
#define HTTP_REQ_TIMEOUT_MS     5000    // 要求がそろわないまま、この時間たったら閉じる
// Close if not a single byte can be sent for this time
#define HTTP_SEND_TIMEOUT_MS    5000    // 1バイトも送れないまま、この時間たったら閉じる
#define HTTP_TASK_STACK         6144
#define HTTP_TASK_PRIO          5

static const char HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>American BackView</title>"
    "<style>"
    "html,body{margin:0;padding:0;background:#000;color:#fff;font-family:sans-serif}"
    "h1{margin:0;padding:8px;font-size:20px;text-align:center}"
    "img{display:block;width:100%;height:auto}"
    "</style>"
    "</head><body>"
    "<h1>American BackView</h1>"
    "<img src=\"/stream\">"
    "</body></html>";

static const char STREAM_HEAD[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "\r\n";

static const char PART_TAIL[] = "\r\n";

static int64_t now_ms()
{
    return esp_timer_get_time() / 1000;
}

static bool set_nonblock(int s)
{
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0) return false;
    return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0;
}

static bool would_block()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

hiraHttpServer::hiraHttpServer(backViewEcst *p)
    : parent(p), listen_sock(-1), frame_active(false), frame_data(nullptr), frame_len(0),
      last_frame_ms(0), capture_ok(true)
{
    for (auto &c : conns) {
        c.sock = -1;
        c.state = ST_FREE;
    }
}

hiraHttpServer::~hiraHttpServer()
{
    for (auto &c : conns) {
        if (c.sock >= 0) closeConn(c, "shutdown");
    }
    if (frame_active) parent->getHCA()->release();
    if (listen_sock >= 0) close(listen_sock);
}

bool hiraHttpServer::GoThread()
{
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) {
        std::printf("HHS: socket failed (%d)\n", errno);
        return false;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        std::printf("HHS: bind failed (%d)\n", errno);
        close(s);
        return false;
    }
    if (listen(s, HTTP_BACKLOG) != 0) {
        std::printf("HHS: listen failed (%d)\n", errno);
        close(s);
        return false;
    }
    if (!set_nonblock(s)) {
        std::printf("HHS: nonblock failed (%d)\n", errno);
        close(s);
        return false;
    }
    listen_sock = s;

    if (xTaskCreate(&hiraHttpServer::task, "hhs_task", HTTP_TASK_STACK, this, HTTP_TASK_PRIO, nullptr) != pdPASS) {
        std::printf("HHS: task create failed\n");
        close(listen_sock);
        listen_sock = -1;
        return false;
    }
    std::printf("HHS: listening on port %d\n", HTTP_PORT);
    return true;
}

void hiraHttpServer::task(void *arg)
{
    static_cast<hiraHttpServer *>(arg)->loop();
    vTaskDelete(nullptr);
}

bool hiraHttpServer::isSending(const Conn &c) const
{
    return c.state == ST_REPLY || c.state == ST_STREAM_HEAD || c.state == ST_STREAM_SEND;
}

void hiraHttpServer::loop()
{
    for (;;) {
        // Wait up to HTTP_WAIT_MS until a socket is readable or writable. Sockets stay non-blocking
        // 読める・書けるソケットが出るまで、最大 HTTP_WAIT_MS 待つ。ソケットはノンブロッキングのまま
        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = listen_sock;
        FD_SET(listen_sock, &rfds);
        for (auto &c : conns) {
            if (c.sock < 0) continue;
            FD_SET(c.sock, &rfds);
            if (isSending(c)) FD_SET(c.sock, &wfds);
            if (c.sock > maxfd) maxfd = c.sock;
        }
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = HTTP_WAIT_MS * 1000;
        int n = select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
        if (n < 0) {
            FD_ZERO(&rfds);
            vTaskDelay(pdMS_TO_TICKS(HTTP_WAIT_MS));
        }
        int64_t now = now_ms();

        if (FD_ISSET(listen_sock, &rfds)) acceptNew(now);
        for (auto &c : conns) {
            if (c.sock >= 0 && FD_ISSET(c.sock, &rfds)) readConn(c, now);
        }
        startFrame(now);
        for (auto &c : conns) {
            if (c.sock >= 0 && isSending(c)) writeConn(c, now);
        }
        endFrameIfDone();
        for (auto &c : conns) {
            if (c.sock >= 0) checkTimeout(c, now);
        }
        endFrameIfDone();
    }
}

void hiraHttpServer::acceptNew(int64_t now)
{
    for (;;) {
        struct sockaddr_in addr = {};
        socklen_t al = sizeof(addr);
        int s = accept(listen_sock, reinterpret_cast<struct sockaddr *>(&addr), &al);
        // No more incoming (EAGAIN) or an error
        if (s < 0) return;   // もう来ていない（EAGAIN）か、エラー

        char ip[16];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));

        Conn *slot = nullptr;
        for (auto &c : conns) {
            if (c.sock < 0) {
                slot = &c;
                break;
            }
        }
        if (!slot || !set_nonblock(s)) {
            std::printf("HHS: reject %s (no slot)\n", ip);
            close(s);
            continue;
        }
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        slot->sock = s;
        slot->state = ST_READ;
        slot->req_len = 0;
        slot->req[0] = '\0';
        slot->piece_count = 0;
        slot->piece_idx = 0;
        slot->piece_pos = 0;
        slot->last_ms = now;
        std::printf("HHS: accept %s (slot %d)\n", ip, static_cast<int>(slot - conns));
    }
}

void hiraHttpServer::readConn(Conn &c, int64_t now)
{
    int r;
    if (c.state == ST_READ) {
        size_t space = sizeof(c.req) - 1 - c.req_len;
        if (space == 0) {
            closeConn(c, "request too long");
            return;
        }
        r = recv(c.sock, c.req + c.req_len, space, MSG_DONTWAIT);
    } else {
        // Discard anything that arrives after the request was read (read to notice when the peer closes)
        // 要求を読み終えたあとに届いたものは捨てる（相手が閉じたのを知るために読む）
        char tmp[128];
        r = recv(c.sock, tmp, sizeof(tmp), MSG_DONTWAIT);
    }

    if (r == 0) {
        closeConn(c, "closed by peer");
        return;
    }
    if (r < 0) {
        if (!would_block()) closeConn(c, "recv error");
        return;
    }
    if (c.state != ST_READ) return;

    c.req_len += r;
    c.req[c.req_len] = '\0';
    c.last_ms = now;
    if (std::strstr(c.req, "\r\n\r\n")) handleRequest(c, now);
}

void hiraHttpServer::handleRequest(Conn &c, int64_t now)
{
    char method[8];
    char path[128];
    if (std::sscanf(c.req, "%7s %127s", method, path) != 2) {
        closeConn(c, "bad request");
        return;
    }
    char *q = std::strchr(path, '?');
    if (q) *q = '\0';
    std::printf("HHS: %s %s\n", method, path);

    if (std::strcmp(method, "GET") != 0) {
        reply(c, "404 Not Found", "text/plain; charset=utf-8", "Not Found\n", now);
        return;
    }
    if (std::strcmp(path, "/") == 0) {
        reply(c, "200 OK", "text/html; charset=utf-8", HTML, now);
        return;
    }
    if (std::strcmp(path, "/stream") == 0) {
        if (countStream() >= MAX_STREAM) {
            reply(c, "503 Service Unavailable", "text/plain; charset=utf-8", "使用中です\n", now);
            return;
        }
        if (!parent->getHCA()->isReady()) {
            reply(c, "503 Service Unavailable", "text/plain; charset=utf-8", "Camera not available\n", now);
            return;
        }
        c.pieces[0] = {reinterpret_cast<const uint8_t *>(STREAM_HEAD), sizeof(STREAM_HEAD) - 1};
        c.piece_count = 1;
        c.piece_idx = 0;
        c.piece_pos = 0;
        c.state = ST_STREAM_HEAD;
        c.last_ms = now;
        return;
    }
    reply(c, "404 Not Found", "text/plain; charset=utf-8", "Not Found\n", now);
}

void hiraHttpServer::reply(Conn &c, const char *status, const char *type, const char *body, int64_t now)
{
    size_t body_len = std::strlen(body);
    int hl = std::snprintf(c.head, sizeof(c.head),
                           "HTTP/1.1 %s\r\n"
                           "Content-Type: %s\r\n"
                           "Content-Length: %u\r\n"
                           "Cache-Control: no-store\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           status, type, static_cast<unsigned>(body_len));
    c.pieces[0] = {reinterpret_cast<const uint8_t *>(c.head), static_cast<size_t>(hl)};
    c.pieces[1] = {reinterpret_cast<const uint8_t *>(body), body_len};
    c.piece_count = 2;
    c.piece_idx = 0;
    c.piece_pos = 0;
    c.state = ST_REPLY;
    c.last_ms = now;
}

void hiraHttpServer::writeConn(Conn &c, int64_t now)
{
    int r = sendPieces(c, now);
    // In progress, or closed
    if (r != 1) return;   // 途中か、閉じた

    switch (c.state) {
    case ST_REPLY:
        closeConn(c, "reply done");
        break;
    case ST_STREAM_HEAD:
        std::printf("HHS: stream start (slot %d)\n", static_cast<int>(&c - conns));
        c.state = ST_STREAM_WAIT;
        break;
    case ST_STREAM_SEND:
        c.state = ST_STREAM_WAIT;
        break;
    default:
        break;
    }
}

int hiraHttpServer::sendPieces(Conn &c, int64_t now)
{
    while (c.piece_idx < c.piece_count) {
        const Piece &p = c.pieces[c.piece_idx];
        size_t rest = p.n - c.piece_pos;
        if (rest == 0) {
            c.piece_idx++;
            c.piece_pos = 0;
            continue;
        }
        int r = send(c.sock, p.p + c.piece_pos, rest, MSG_DONTWAIT);
        if (r > 0) {
            c.piece_pos += r;
            c.last_ms = now;
            continue;
        }
        // Send buffer is full. Send the rest in the next loop
        if (r == 0 || would_block()) return 0;   // 送信バッファが一杯。次の周回で続きを送る
        closeConn(c, "send error");
        return -1;
    }
    return 1;
}

// If the previous frame has been sent to everyone, someone is waiting, and the interval has passed, take one frame and hand it out
// 前の1枚を全員に送り終えていて、待っている相手がいて、間隔が空いていたら、1枚撮って配る
void hiraHttpServer::startFrame(int64_t now)
{
    if (frame_active) return;
    bool waiting = false;
    for (auto &c : conns) {
        if (c.sock >= 0 && c.state == ST_STREAM_WAIT) {
            waiting = true;
            break;
        }
    }
    if (!waiting) return;
    if (now - last_frame_ms < HTTP_FRAME_INTERVAL_MS) return;
    last_frame_ms = now;

    hiraCamera *cam = parent->getHCA();
    const uint8_t *data = nullptr;
    size_t len = 0;
    if (!cam->capture(&data, &len)) {
        if (capture_ok) std::printf("HHS: capture failed (%s)\n", cam->getError());
        capture_ok = false;
        return;
    }
    if (!capture_ok) std::printf("HHS: capture ok\n");
    capture_ok = true;

    frame_active = true;
    frame_data = data;
    frame_len = len;
    for (auto &c : conns) {
        if (c.sock < 0 || c.state != ST_STREAM_WAIT) continue;
        int hl = std::snprintf(c.head, sizeof(c.head),
                               "--frame\r\n"
                               "Content-Type: image/jpeg\r\n"
                               "Content-Length: %u\r\n"
                               "\r\n",
                               static_cast<unsigned>(frame_len));
        c.pieces[0] = {reinterpret_cast<const uint8_t *>(c.head), static_cast<size_t>(hl)};
        c.pieces[1] = {frame_data, frame_len};
        c.pieces[2] = {reinterpret_cast<const uint8_t *>(PART_TAIL), sizeof(PART_TAIL) - 1};
        c.piece_count = 3;
        c.piece_idx = 0;
        c.piece_pos = 0;
        c.state = ST_STREAM_SEND;
        c.last_ms = now;
    }
}

// When no one is sending the frame any more, return it to the camera
// 1枚を送っている相手がいなくなったら、フレームをカメラに返す
void hiraHttpServer::endFrameIfDone()
{
    if (!frame_active) return;
    for (auto &c : conns) {
        if (c.sock >= 0 && c.state == ST_STREAM_SEND) return;
    }
    parent->getHCA()->release();
    frame_active = false;
    frame_data = nullptr;
    frame_len = 0;
}

void hiraHttpServer::checkTimeout(Conn &c, int64_t now)
{
    if (c.state == ST_READ && now - c.last_ms > HTTP_REQ_TIMEOUT_MS) {
        closeConn(c, "request timeout");
    } else if (isSending(c) && now - c.last_ms > HTTP_SEND_TIMEOUT_MS) {
        closeConn(c, "send timeout");
    }
}

void hiraHttpServer::closeConn(Conn &c, const char *why)
{
    if (c.sock < 0) return;
    std::printf("HHS: close slot %d (%s)\n", static_cast<int>(&c - conns), why);
    close(c.sock);
    c.sock = -1;
    c.state = ST_FREE;
    c.piece_count = 0;
}

int hiraHttpServer::countStream() const
{
    int n = 0;
    for (const auto &c : conns) {
        if (c.sock < 0) continue;
        if (c.state == ST_STREAM_HEAD || c.state == ST_STREAM_WAIT || c.state == ST_STREAM_SEND) n++;
    }
    return n;
}
