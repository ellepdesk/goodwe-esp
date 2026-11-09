#pragma once

#include <stddef.h>
#include <stdint.h>

#include <usb/usb_host.h>

// The inverter's USB HID port through ESP-IDF's raw USB host API, no HID class driver.
// Layout from `lsusb -v -d 0084:0041` (2026-11-04): full speed, interface 0 with a single
// interrupt IN endpoint 0x81 (32-byte packets) and no OUT endpoint, so output reports go
// as SET_REPORT on the control pipe. Reports: 8 bytes in, 72 bytes out, no report IDs.
//
// Everything, including the transfer callbacks, runs in the task that calls poll().

class UsbHid {
public:
    bool begin(uint16_t vid, uint16_t pid);
    void poll();
    bool connected() const { return dev_ != nullptr && !gone_; }

    // Sends CC 99 <len> + frame as one output report; false if not connected or busy.
    bool write(const uint8_t* frame, size_t len);
    // Why the last write() failed.
    const char* write_error() const { return write_error_; }
    // Bytes from input reports, in order.
    size_t read(uint8_t* buf, size_t size);

private:
    static constexpr uint8_t kInterface = 0;
    static constexpr uint8_t kInEndpoint = 0x81;
    static constexpr int kInPacket = 32;
    static constexpr int kOutReport = 72;

    static void client_event(const usb_host_client_event_msg_t* msg, void* arg);
    static void in_done(usb_transfer_t* t);
    static void out_done(usb_transfer_t* t);
    void open(uint8_t address);
    void close();
    void submit_in();

    uint16_t vid_ = 0, pid_ = 0;
    usb_host_client_handle_t client_ = nullptr;
    usb_device_handle_t dev_ = nullptr;
    uint8_t new_address_ = 0;  // a device to look at on the next poll()
    bool gone_ = false;
    bool claimed_ = false;
    usb_transfer_t* in_ = nullptr;
    usb_transfer_t* out_ = nullptr;
    bool in_busy_ = false, out_busy_ = false;
    const char* write_error_ = "";

    uint8_t rx_[512];
    size_t rx_head_ = 0, rx_tail_ = 0;
};
