#include "usb_hid.h"

#include <Arduino.h>
#include <string.h>

bool UsbHid::begin(uint16_t vid, uint16_t pid) {
    vid_ = vid;
    pid_ = pid;
    usb_host_config_t host = {};
    host.skip_phy_setup = false;
    host.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&host) != ESP_OK) return false;
    usb_host_client_config_t cfg = {};
    cfg.is_synchronous = false;
    cfg.max_num_event_msg = 5;
    cfg.async.client_event_callback = client_event;
    cfg.async.callback_arg = this;
    return usb_host_client_register(&cfg, &client_) == ESP_OK;
}

void UsbHid::client_event(const usb_host_client_event_msg_t* msg, void* arg) {
    UsbHid* self = static_cast<UsbHid*>(arg);
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        self->new_address_ = msg->new_dev.address;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               msg->dev_gone.dev_hdl == self->dev_) {
        self->gone_ = true;
    }
}

void UsbHid::poll() {
    uint32_t flags;
    usb_host_lib_handle_events(0, &flags);
    if (client_) usb_host_client_handle_events(client_, 0);

    if (new_address_ && !dev_) open(new_address_);
    new_address_ = 0;
    // A gone device completes its transfers with NO_DEVICE; release it once they are back.
    if (gone_ && !in_busy_ && !out_busy_) close();
}

void UsbHid::open(uint8_t address) {
    if (usb_host_device_open(client_, address, &dev_) != ESP_OK) {
        dev_ = nullptr;
        return;
    }
    const usb_device_desc_t* desc;
    if (usb_host_get_device_descriptor(dev_, &desc) != ESP_OK || desc->idVendor != vid_ ||
        desc->idProduct != pid_) {
        usb_host_device_close(client_, dev_);
        dev_ = nullptr;
        return;
    }
    if (usb_host_interface_claim(client_, dev_, kInterface, 0) != ESP_OK ||
        usb_host_transfer_alloc(kInPacket, 0, &in_) != ESP_OK ||
        usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + kOutReport, 0, &out_) != ESP_OK) {
        gone_ = true;  // close() cleans up whatever was set up
        return;
    }
    claimed_ = true;
    gone_ = false;
    rx_head_ = rx_tail_ = 0;
    in_->device_handle = dev_;
    in_->bEndpointAddress = kInEndpoint;
    in_->callback = in_done;
    in_->context = this;
    in_->num_bytes = kInPacket;
    out_->device_handle = dev_;
    out_->bEndpointAddress = 0;
    out_->callback = out_done;
    out_->context = this;
    submit_in();
}

void UsbHid::close() {
    if (claimed_) usb_host_interface_release(client_, dev_, kInterface);
    claimed_ = false;
    if (in_) usb_host_transfer_free(in_);
    if (out_) usb_host_transfer_free(out_);
    in_ = out_ = nullptr;
    if (dev_) usb_host_device_close(client_, dev_);
    dev_ = nullptr;
    gone_ = false;
}

void UsbHid::submit_in() {
    if (gone_ || !in_) return;
    in_busy_ = usb_host_transfer_submit(in_) == ESP_OK;
}

void UsbHid::in_done(usb_transfer_t* t) {
    UsbHid* self = static_cast<UsbHid*>(t->context);
    self->in_busy_ = false;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        for (int i = 0; i < t->actual_num_bytes; i++) {
            size_t next = (self->rx_head_ + 1) % sizeof self->rx_;
            if (next == self->rx_tail_) break;  // full: drop, the parser resyncs
            self->rx_[self->rx_head_] = t->data_buffer[i];
            self->rx_head_ = next;
        }
    }
    if (t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        self->gone_ = true;
        return;
    }
    self->submit_in();
}

void UsbHid::out_done(usb_transfer_t* t) {
    UsbHid* self = static_cast<UsbHid*>(t->context);
    self->out_busy_ = false;
    if (t->status == USB_TRANSFER_STATUS_NO_DEVICE) self->gone_ = true;
}

bool UsbHid::write(const uint8_t* frame, size_t len) {
    if (!connected() || out_busy_ || len + 3 > size_t(kOutReport)) {
        write_error_ = !connected() ? "not connected" : out_busy_ ? "previous report still busy"
                                                                  : "frame too long";
        return false;
    }
    usb_setup_packet_t* setup = reinterpret_cast<usb_setup_packet_t*>(out_->data_buffer);
    setup->bmRequestType = 0x21;  // host to device, class, interface
    setup->bRequest = 0x09;       // SET_REPORT
    setup->wValue = 0x0200;       // output report, ID 0
    setup->wIndex = kInterface;
    setup->wLength = kOutReport;
    uint8_t* report = out_->data_buffer + USB_SETUP_PACKET_SIZE;
    memset(report, 0, kOutReport);
    report[0] = 0xCC;
    report[1] = 0x99;
    report[2] = uint8_t(len);
    memcpy(report + 3, frame, len);
    out_->num_bytes = USB_SETUP_PACKET_SIZE + kOutReport;
    esp_err_t err = usb_host_transfer_submit_control(client_, out_);
    out_busy_ = err == ESP_OK;
    if (!out_busy_) write_error_ = esp_err_to_name(err);
    return out_busy_;
}

size_t UsbHid::read(uint8_t* buf, size_t size) {
    size_t n = 0;
    while (n < size && rx_tail_ != rx_head_) {
        buf[n++] = rx_[rx_tail_];
        rx_tail_ = (rx_tail_ + 1) % sizeof rx_;
    }
    return n;
}
