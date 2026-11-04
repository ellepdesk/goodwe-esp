// Long-running bridge from the inverter (USB HID or serial) to Home Assistant over MQTT:
// the Linux counterpart of the ESP firmware. Publishes the same discovery and state as
// goodwe_logging_proxy and, with --allow-control, a power limit number entity.

#include <getopt.h>
#include <mosquitto.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aa55.h"
#include "bridge.h"
#include "ha.h"
#include "transport.h"

namespace {

std::atomic<bool> g_stop{false};
std::atomic<int> g_connects{0};
// Messages arrive on the mosquitto thread; the bridge runs on the main thread.
std::mutex g_messages_mutex;
std::vector<std::pair<std::string, std::string>> g_messages;

void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log(const char* fmt, ...) {
    // One write per line: the MQTT thread logs too.
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", line);
}

void on_connect(mosquitto*, void*, int rc) {
    if (rc == 0) {
        log("mqtt connected");
        g_connects++;
    } else {
        log("mqtt connect refused: %s", mosquitto_connack_string(rc));
    }
}

void on_disconnect(mosquitto*, void*, int rc) {
    if (rc != 0) log("mqtt connection lost, reconnecting");
}

void on_message(mosquitto*, void*, const mosquitto_message* m) {
    std::lock_guard<std::mutex> lock(g_messages_mutex);
    g_messages.push_back(
        {m->topic, std::string(static_cast<const char*>(m->payload), size_t(m->payloadlen))});
}

// Sleeps up to seconds, waking early on SIGTERM/SIGINT.
void nap(int seconds) {
    for (int i = 0; i < seconds * 10 && !g_stop; i++) usleep(100000);
}

void usage() {
    fprintf(stderr,
            "usage: goodwe-mqtt (--hid[=VID:PID] | --serial DEV) [options]\n"
            "\n"
            "options:\n"
            "  --hid[=VID:PID]   USB HID, default 0084:0041\n"
            "  --report-size N   pad HID output reports to N bytes (default: no padding)\n"
            "  --serial DEV      serial port on the inverter UART or RS485\n"
            "  --baud N          serial speed, default 9600\n"
            "  --address N       address to assign to the inverter, default 0x11\n"
            "  --poll SEC        running-info interval, default 10\n"
            "  --broker HOST     MQTT broker, default localhost\n"
            "  --port N          MQTT port, default 1883\n"
            "  --prefix P        discovery prefix, default homeassistant\n"
            "  --allow-control   add Power Limit number entities (%% and W) and Stop/Start\n"
            "                    buttons (disabled in HA by default); without it they\n"
            "                    are removed\n"
            "  --rated-power W   rated power for the W entity, default from the serial\n"
            "                    (1 3600 DS... -> 3600)\n"
            "  --raw             print all bytes sent and received\n"
            "\n"
            "MQTT_USERNAME and MQTT_PASSWORD are read from the environment.\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool use_hid = false, raw = false;
    unsigned vid = 0x0084, pid = 0x0041;
    size_t report_size = 0;
    const char* serial_path = nullptr;
    int baud = AA55_BAUD_DEFAULT;
    uint8_t address = 0x11;
    int poll_s = 10;
    std::string broker = "localhost";
    int port = 1883;
    ha::Config config;

    enum { kHid = 1, kReportSize, kSerial, kBaud, kAddress, kPoll, kBroker, kPort, kPrefix,
           kAllow, kRated, kRaw };
    static const option opts[] = {
        {"hid", optional_argument, nullptr, kHid},
        {"report-size", required_argument, nullptr, kReportSize},
        {"serial", required_argument, nullptr, kSerial},
        {"baud", required_argument, nullptr, kBaud},
        {"address", required_argument, nullptr, kAddress},
        {"poll", required_argument, nullptr, kPoll},
        {"broker", required_argument, nullptr, kBroker},
        {"port", required_argument, nullptr, kPort},
        {"prefix", required_argument, nullptr, kPrefix},
        {"allow-control", no_argument, nullptr, kAllow},
        {"rated-power", required_argument, nullptr, kRated},
        {"raw", no_argument, nullptr, kRaw},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };
    for (int c; (c = getopt_long(argc, argv, "h", opts, nullptr)) != -1;) {
        switch (c) {
        case kHid:
            use_hid = true;
            if (optarg && sscanf(optarg, "%x:%x", &vid, &pid) != 2) {
                usage();
                return 2;
            }
            break;
        case kReportSize: report_size = strtoul(optarg, nullptr, 0); break;
        case kSerial: serial_path = optarg; break;
        case kBaud: baud = atoi(optarg); break;
        case kAddress: address = uint8_t(strtoul(optarg, nullptr, 0)); break;
        case kPoll: poll_s = atoi(optarg); break;
        case kBroker: broker = optarg; break;
        case kPort: port = atoi(optarg); break;
        case kPrefix: config.prefix = optarg; break;
        case kAllow: config.power_limit = true; break;
        case kRated: config.rated_power = atoi(optarg); break;
        case kRaw: raw = true; break;
        default: usage(); return 2;
        }
    }
    if (optind != argc || use_hid == bool(serial_path)) {
        usage();
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);

    struct sigaction sa{};
    sa.sa_handler = [](int) { g_stop = true; };
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    mosquitto_lib_init();
    mosquitto* mq = mosquitto_new("goodwe-mqtt", true, nullptr);
    if (!mq) {
        log("mosquitto_new failed");
        return 1;
    }
    const char* user = getenv("MQTT_USERNAME");
    if (user) mosquitto_username_pw_set(mq, user, getenv("MQTT_PASSWORD"));
    mosquitto_connect_callback_set(mq, on_connect);
    mosquitto_disconnect_callback_set(mq, on_disconnect);
    mosquitto_message_callback_set(mq, on_message);
    mosquitto_reconnect_delay_set(mq, 2, 60, true);
    // The network thread keeps retrying, so a broker that is down at boot is fine.
    int rc = mosquitto_connect_async(mq, broker.c_str(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) log("mqtt %s:%d: %s, retrying", broker.c_str(), port,
                                    mosquitto_strerror(rc));
    mosquitto_loop_start(mq);

    Transport* inverter = nullptr;
    ha::Bridge::Io io;
    io.publish = [mq](const std::string& topic, const std::string& payload, bool retain) {
        mosquitto_publish(mq, nullptr, topic.c_str(), int(payload.size()), payload.data(), 0,
                          retain);
    };
    io.subscribe = [mq](const std::string& topic) {
        mosquitto_subscribe(mq, nullptr, topic.c_str(), 1);
    };
    io.send = [&](const uint8_t* f, size_t n) {
        if (raw) hexdump("tx", f, n);
        if (inverter && !inverter->write(f, n)) log("write failed");
    };
    io.log = [](const char* line) { log("%s", line); };
    ha::Bridge bridge(config, io, address, uint32_t(poll_s) * 1000);
    std::string last_error;
    int connects = 0;

    while (!g_stop) {
        std::string error;
        std::unique_ptr<Transport> t = use_hid ? open_hid(vid, pid, report_size, error)
                                               : open_serial(serial_path, baud, error);
        if (!t) {
            // The inverter's USB port disappears every night; say so once, not every retry.
            if (error != last_error) log("%s; retrying every 10 s", error.c_str());
            last_error = error;
            nap(10);
            continue;
        }
        last_error.clear();
        inverter = t.get();
        bridge.port_opened();

        uint8_t buf[64];
        while (!g_stop) {
            int n = t->read(buf, sizeof buf, 50);
            if (n < 0) break;
            if (raw && n > 0) hexdump("rx", buf, size_t(n));
            uint32_t now = now_ms();
            bridge.feed(buf, size_t(n > 0 ? n : 0), now);

            if (g_connects != connects) {
                connects = g_connects;
                bridge.mqtt_connected();
            }
            std::vector<std::pair<std::string, std::string>> messages;
            {
                std::lock_guard<std::mutex> lock(g_messages_mutex);
                messages.swap(g_messages);
            }
            for (const auto& m : messages) bridge.message(m.first, m.second);
            bridge.tick(now);
        }
        bridge.port_closed();
        inverter = nullptr;
        if (!g_stop) nap(2);
    }

    log("stopping");
    mosquitto_disconnect(mq);
    mosquitto_loop_stop(mq, false);
    mosquitto_destroy(mq);
    mosquitto_lib_cleanup();
    return 0;
}
