#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <httplib.h>

// ─── Shared frame buffer ─────────────────────────────────────────────

struct FrameBuffer {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> jpeg;
    std::atomic<uint64_t> frame_id{0};

    void update(const std::vector<uint8_t>& data) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            jpeg = data;
        }
        frame_id++;
        cv.notify_all();
    }

    bool get(std::vector<uint8_t>& out, uint64_t& last_seen, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx);
        if (frame_id.load() == last_seen) {
            return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this, &last_seen] { return frame_id.load() != last_seen; });
        }
        last_seen = frame_id.load();
        out = jpeg;
        return !out.empty();
    }

    void snapshot(std::vector<uint8_t>& out) {
        std::lock_guard<std::mutex> lock(mtx);
        out = jpeg;
    }
};

static std::atomic<bool> g_running{true};

// ─── Signal handler ──────────────────────────────────────────────────

static void signal_handler(int) {
    g_running = false;
}

// ─── Capture loop ────────────────────────────────────────────────────

struct CaptureConfig {
    int device_index = 0;
    std::string device_path;
    int width = 640;
    int height = 480;
    int fps = 15;
    int quality = 70;
};

static void capture_loop(FrameBuffer& buf, const CaptureConfig& cfg) {
    cv::VideoCapture cap;

    if (!cfg.device_path.empty()) {
        // Open by device path string (supports GStreamer pipelines)
        cap.open(cfg.device_path, cv::CAP_V4L2);
    } else {
        cap.open(cfg.device_index, cv::CAP_V4L2);
    }

    if (!cap.isOpened()) {
        fprintf(stderr, "[capture] ERROR: Cannot open camera (device %s%d)\n",
                cfg.device_path.empty() ? "" : cfg.device_path.c_str(),
                cfg.device_path.empty() ? cfg.device_index : 0);
        g_running = false;
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, cfg.width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.height);
    cap.set(cv::CAP_PROP_FPS, cfg.fps);

    fprintf(stderr, "[capture] Camera opened: %dx%d @ %d fps\n",
            (int)cap.get(cv::CAP_PROP_FRAME_WIDTH),
            (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT),
            (int)cap.get(cv::CAP_PROP_FPS));

    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, cfg.quality};
    cv::Mat frame;

    while (g_running) {
        if (!cap.read(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (frame.empty()) continue;

        // 逆时针旋转 90 度以修正画面方向
        cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);

        std::vector<uint8_t> jpeg_buf;
        if (cv::imencode(".jpg", frame, jpeg_buf, params)) {
            buf.update(jpeg_buf);
        }
    }

    cap.release();
    fprintf(stderr, "[capture] Stopped\n");
}

// ─── Usage ───────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --port <N>        HTTP port (default: 8080)\n"
        "  --device <path>   Camera device, e.g. /dev/video0 (default: index 0)\n"
        "  --width <N>       Frame width (default: 640)\n"
        "  --height <N>      Frame height (default: 480)\n"
        "  --fps <N>         Target FPS (default: 15)\n"
        "  --quality <N>     JPEG quality 1-100 (default: 70)\n"
        "  --help            Show this help\n",
        prog);
}

// ─── Main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    CaptureConfig cap_cfg;
    int port = 8080;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--device" && i + 1 < argc) {
            cap_cfg.device_path = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            cap_cfg.width = std::atoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            cap_cfg.height = std::atoi(argv[++i]);
        } else if (arg == "--fps" && i + 1 < argc) {
            cap_cfg.fps = std::atoi(argv[++i]);
        } else if (arg == "--quality" && i + 1 < argc) {
            cap_cfg.quality = std::atoi(argv[++i]);
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    FrameBuffer frame_buf;

    // Start capture thread
    std::thread capture_thread(capture_loop, std::ref(frame_buf), std::cref(cap_cfg));

    // HTTP server
    httplib::Server svr;

    // GET / — JSON status
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "{\"status\":\"running\",\"endpoints\":[\"/stream\",\"/snapshot\"]}",
            "application/json");
    });

    // GET /snapshot — single JPEG frame (waits for first frame)
    svr.Get("/snapshot", [&frame_buf](const httplib::Request&, httplib::Response& res) {
        std::vector<uint8_t> jpeg;
        uint64_t last_seen = 0;
        if (!frame_buf.get(jpeg, last_seen, 3000) || jpeg.empty()) {
            res.status = 503;
            res.set_content("{\"error\":\"no frame yet\"}", "application/json");
            return;
        }
        res.set_content(reinterpret_cast<const char*>(jpeg.data()),
                        jpeg.size(), "image/jpeg");
    });

    // GET /stream — MJPEG multipart stream
    svr.Get("/stream", [&frame_buf](const httplib::Request&,
                                      httplib::Response& res) {
        res.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [&frame_buf](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                uint64_t last_seen = 0;
                while (g_running) {
                    std::vector<uint8_t> jpeg;
                    if (!frame_buf.get(jpeg, last_seen, 2000)) {
                        // timeout — send keep-alive or exit
                        if (!g_running) return false;
                        continue;
                    }

                    // Build MJPEG frame
                    std::string header =
                        "--frame\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: " +
                        std::to_string(jpeg.size()) + "\r\n\r\n";

                    sink.write(header.c_str(), header.size());
                    sink.write(reinterpret_cast<const char*>(jpeg.data()),
                               jpeg.size());
                    sink.write("\r\n", 2);
                }
                return false;
            });
    });

    fprintf(stderr, "[server] Starting on port %d\n", port);
    if (!svr.listen("0.0.0.0", port)) {
        fprintf(stderr, "[server] ERROR: Cannot bind port %d\n", port);
        g_running = false;
    }

    g_running = false;
    capture_thread.join();

    return 0;
}
