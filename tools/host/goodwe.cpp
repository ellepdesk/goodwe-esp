// Host CLI (Linux, macOS) running the same lib/aa55 code as the ESP firmware, over the
// inverter's USB HID port or a serial adapter on the inverter UART / RS485.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <hidapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <string>

#include "aa55.h"
#include "master.h"

namespace {

struct Transport {
    virtual ~Transport() = default;
    virtual bool write(const uint8_t* frame, size_t len) = 0;
    // Returns the number of bytes read (0 on timeout), or -1 on error.
    virtual int read(uint8_t* buf, size_t size, int timeout_ms) = 0;
};

// GoodWe's USB port is a HID device. Frames go out as an output report prefixed with
// CC 99 <len>; replies arrive as a plain AA55 byte stream split over input reports
// (per GoodWeUSBLogger and Goodwe2PVoutput).
class HidTransport : public Transport {
public:
    HidTransport(hid_device* dev, size_t report_size) : dev_(dev), report_size_(report_size) {}
    ~HidTransport() override { hid_close(dev_); }

    bool write(const uint8_t* frame, size_t len) override {
        uint8_t buf[1 + 3 + aa55::kMaxFrame] = {0};  // leading 0: no report ID
        buf[1] = 0xCC;
        buf[2] = 0x99;
        buf[3] = uint8_t(len);
        memcpy(buf + 4, frame, len);
        size_t n = 4 + len;
        if (report_size_ + 1 > n) n = report_size_ + 1;
        return hid_write(dev_, buf, n) >= 0;
    }

    int read(uint8_t* buf, size_t size, int timeout_ms) override {
        return hid_read_timeout(dev_, buf, size, timeout_ms);
    }

private:
    hid_device* dev_;
    size_t report_size_;
};

class SerialTransport : public Transport {
public:
    explicit SerialTransport(int fd) : fd_(fd) {}
    ~SerialTransport() override { close(fd_); }

    bool write(const uint8_t* frame, size_t len) override {
        return ::write(fd_, frame, len) == ssize_t(len);
    }

    int read(uint8_t* buf, size_t size, int timeout_ms) override {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_, &fds);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) return r;
        return int(::read(fd_, buf, size));
    }

private:
    int fd_;
};

speed_t baud_constant(int baud) {
    switch (baud) {
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return 0;
    }
}

std::unique_ptr<Transport> open_serial(const char* path, int baud) {
    speed_t speed = baud_constant(baud);
    if (!speed) {
        fprintf(stderr, "unsupported baud rate %d\n", baud);
        return nullptr;
    }
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return nullptr;
    }
    termios tio{};
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tcsetattr(fd, TCSANOW, &tio);
    tcflush(fd, TCIOFLUSH);
    return std::make_unique<SerialTransport>(fd);
}

std::unique_ptr<Transport> open_hid(unsigned vid, unsigned pid, size_t report_size) {
    if (hid_init()) return nullptr;
    hid_device* dev = hid_open(vid, pid, nullptr);
    if (!dev) {
        fprintf(stderr, "no HID device %04x:%04x (permissions? see tools/host/README.md)\n",
                vid, pid);
        return nullptr;
    }
    return std::make_unique<HidTransport>(dev, report_size);
}

uint32_t now_ms() {
    using namespace std::chrono;
    return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void hexdump(const char* tag, const uint8_t* b, size_t n) {
    printf("%s", tag);
    for (size_t i = 0; i < n; i++) printf(" %02X", b[i]);
    printf("\n");
    fflush(stdout);
}

void print_frame(const aa55::Frame& f) {
    printf("frame src=%02X dst=%02X ctrl=%02X func=%02X len=%u", f.src, f.dst, f.ctrl, f.func,
           f.len);
    for (int i = 0; i < f.len; i++) printf(" %02X", f.data[i]);
    printf("\n");
    fflush(stdout);
}

void print_info(const aa55::RunningInfo& i) {
    printf("pv %.1fV %.1fA / %.1fV %.1fA  ac %.1fV %.1fA %.2fHz %uW  mode %u  %.1fC  "
           "err %08X  day %.1fkWh total %.1fkWh %uh\n",
           i.vpv1, i.ipv1, i.vpv2, i.ipv2, i.vac, i.iac, i.fac, i.pac, i.work_mode,
           i.temperature, i.error_code, i.e_day, i.e_total, i.h_total);
    fflush(stdout);
}

void usage() {
    fprintf(stderr,
            "usage: goodwe (--hid[=VID:PID] | --serial DEV) [options] COMMAND\n"
            "\n"
            "commands:\n"
            "  sniff        print every frame, send nothing\n"
            "  monitor      register the inverter and print running info each poll\n"
            "  power PCT    limit real power to PCT %% of rated (0-100)\n"
            "  start, stop  start or stop the inverter\n"
            "\n"
            "options:\n"
            "  --hid[=VID:PID]   USB HID, default 0084:0041\n"
            "  --report-size N   pad HID output reports to N bytes (default: no padding)\n"
            "  --serial DEV      serial port on the inverter UART or RS485\n"
            "  --baud N          serial speed, default 9600\n"
            "  --address N       address to assign to the inverter, default 0x11\n"
            "  --poll SEC        running-info interval for monitor, default 10\n"
            "  --raw             print all bytes sent and received\n"
            "  --allow-control   required for power/start/stop\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool use_hid = false, raw = false, allow_control = false;
    unsigned vid = 0x0084, pid = 0x0041;
    size_t report_size = 0;
    const char* serial_path = nullptr;
    int baud = AA55_BAUD_DEFAULT;
    uint8_t address = 0x11;
    int poll_s = 10;

    enum { kHid = 1, kReportSize, kSerial, kBaud, kAddress, kPoll, kRaw, kAllow };
    static const option opts[] = {
        {"hid", optional_argument, nullptr, kHid},
        {"report-size", required_argument, nullptr, kReportSize},
        {"serial", required_argument, nullptr, kSerial},
        {"baud", required_argument, nullptr, kBaud},
        {"address", required_argument, nullptr, kAddress},
        {"poll", required_argument, nullptr, kPoll},
        {"raw", no_argument, nullptr, kRaw},
        {"allow-control", no_argument, nullptr, kAllow},
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
        case kRaw: raw = true; break;
        case kAllow: allow_control = true; break;
        default: usage(); return 2;
        }
    }
    if (optind >= argc || use_hid == bool(serial_path)) {
        usage();
        return 2;
    }
    std::string cmd = argv[optind];

    uint8_t exec_func = 0, exec_data = 0, exec_len = 0;
    if (cmd == "power") {
        if (optind + 1 >= argc) {
            usage();
            return 2;
        }
        int pct = atoi(argv[optind + 1]);
        if (pct < 0 || pct > 100) {
            fprintf(stderr, "power must be 0-100\n");
            return 2;
        }
        exec_func = aa55::kRealPowerAdjust;
        exec_data = uint8_t(pct);
        exec_len = 1;
    } else if (cmd == "start") {
        exec_func = aa55::kStart;
    } else if (cmd == "stop") {
        exec_func = aa55::kStop;
    } else if (cmd != "sniff" && cmd != "monitor") {
        usage();
        return 2;
    }
    if (exec_func && !allow_control) {
        fprintf(stderr, "'%s' changes inverter behaviour; pass --allow-control\n", cmd.c_str());
        return 2;
    }

    std::unique_ptr<Transport> t =
        use_hid ? open_hid(vid, pid, report_size) : open_serial(serial_path, baud);
    if (!t) return 1;

    int exit_code = -1;
    aa55::Master::Callbacks cb;
    cb.send = [&](const uint8_t* f, size_t n) {
        if (raw) hexdump("tx", f, n);
        if (!t->write(f, n)) fprintf(stderr, "write failed\n");
    };
    cb.on_frame = [&](const aa55::Frame& f) {
        if (raw) print_frame(f);
    };
    cb.on_registered = [&](const char* serial) {
        printf("registered inverter %s at address 0x%02X\n", serial, address);
        fflush(stdout);
    };
    cb.on_info = print_info;
    cb.on_execute = [&](uint8_t func, bool ack) {
        printf("command %02X: %s\n", func, ack ? "ACK" : "NAK or no reply");
        exit_code = ack ? 0 : 1;
    };
    aa55::Master master(cb, address, uint32_t(poll_s) * 1000);
    if (exec_func) master.execute(exec_func, &exec_data, exec_len);

    aa55::Parser sniffer;
    uint8_t buf[64];
    for (;;) {
        int n = t->read(buf, sizeof buf, 50);
        if (n < 0) {
            fprintf(stderr, "read failed\n");
            return 1;
        }
        if (raw && n > 0) hexdump("rx", buf, size_t(n));
        uint32_t now = now_ms();
        for (int i = 0; i < n; i++) {
            if (cmd == "sniff") {
                if (sniffer.feed(buf[i])) {
                    print_frame(sniffer.frame());
                    aa55::RunningInfo info;
                    const aa55::Frame& f = sniffer.frame();
                    if (f.ctrl == aa55::kRead &&
                        f.func == (aa55::kRunningInfo | aa55::kResponse) &&
                        aa55::decode_running_info(f.data, f.len, info))
                        print_info(info);
                }
            } else {
                master.feed(buf[i], now);
            }
        }
        if (cmd != "sniff") master.tick(now);
        if (exit_code >= 0) return exit_code;
    }
}
