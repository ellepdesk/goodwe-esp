#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <hidapi.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>

#include "aa55.h"

namespace {

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
        if (r < 0 && errno == EINTR) return 0;
        if (r <= 0) return r;
        ssize_t n = ::read(fd_, buf, size);
        // A USB-serial adapter that is unplugged reads as end of file.
        return n > 0 ? int(n) : -1;
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

}  // namespace

std::unique_ptr<Transport> open_serial(const char* path, int baud, std::string& error) {
    speed_t speed = baud_constant(baud);
    if (!speed) {
        error = "unsupported baud rate " + std::to_string(baud);
        return nullptr;
    }
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        error = std::string(path) + ": " + strerror(errno);
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

std::unique_ptr<Transport> open_hid(unsigned vid, unsigned pid, size_t report_size,
                                    std::string& error) {
    if (hid_init()) {
        error = "hid_init failed";
        return nullptr;
    }
    hid_device* dev = hid_open(vid, pid, nullptr);
    if (!dev) {
        char buf[96];
        snprintf(buf, sizeof buf,
                 "no HID device %04x:%04x (unplugged, inverter off, or permissions)", vid, pid);
        error = buf;
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
