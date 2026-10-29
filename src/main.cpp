// Phase 3 sniffer: listens on both data lines between the inverter and the HF-A11 and logs
// every byte and every AA55 frame on the USB console. Receive-only; never drive the bus.

#include <Arduino.h>
#include <aa55.h>

namespace {

struct Tap {
    const char* name;
    HardwareSerial& uart;
    aa55::Parser parser;
    char hex[3 * 64 + 1];
    size_t hex_len;
    uint32_t last_byte_ms;
};

Tap taps[] = {
    {"A11", SNIFF_A11_UART, {}, {}, 0, 0},
    {"INV", SNIFF_INV_UART, {}, {}, 0, 0},
};

// Bytes go out in bursts; a pause this long ends a raw-dump line.
constexpr uint32_t kIdleFlushMs = 20;

void flush_hex(Tap& t) {
    if (!t.hex_len) return;
    Serial.printf("%10lu %s raw %s\n", millis(), t.name, t.hex);
    t.hex_len = 0;
    t.hex[0] = 0;
}

void log_frame(const Tap& t) {
    const aa55::Frame& f = t.parser.frame();
    Serial.printf("%10lu %s frame src=%02X dst=%02X ctrl=%02X func=%02X len=%u\n", millis(),
                  t.name, f.src, f.dst, f.ctrl, f.func, f.len);
    aa55::RunningInfo info;
    if (f.ctrl == aa55::kRead && f.func == (aa55::kRunningInfo | aa55::kResponse) &&
        aa55::decode_running_info(f.data, f.len, info)) {
        Serial.printf("           pv %.1fV %.1fA / %.1fV %.1fA  ac %.1fV %.1fA %.2fHz %uW"
                      "  mode %u  %.1fC  err %08lX  day %.1fkWh total %.1fkWh %luh\n",
                      info.vpv1, info.ipv1, info.vpv2, info.ipv2, info.vac, info.iac, info.fac,
                      info.pac, info.work_mode, info.temperature, (unsigned long)info.error_code,
                      info.e_day, info.e_total, (unsigned long)info.h_total);
    }
}

void poll(Tap& t) {
    while (t.uart.available()) {
        uint8_t b = t.uart.read();
        t.last_byte_ms = millis();
        if (t.hex_len + 3 >= sizeof t.hex) flush_hex(t);
        t.hex_len += snprintf(t.hex + t.hex_len, sizeof t.hex - t.hex_len, "%02X ", b);
        if (t.parser.feed(b)) {
            flush_hex(t);
            log_frame(t);
        }
    }
    if (t.hex_len && millis() - t.last_byte_ms > kIdleFlushMs) flush_hex(t);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    SNIFF_A11_UART.begin(AA55_BAUD, SERIAL_8N1, SNIFF_A11_RX_PIN, -1);
    SNIFF_INV_UART.begin(AA55_BAUD, SERIAL_8N1, SNIFF_INV_RX_PIN, -1);
    // The A11 samples its TXD as a boot strap behind a 1k pull-down; keep our side neutral.
    gpio_pullup_dis(gpio_num_t(SNIFF_A11_RX_PIN));
    gpio_pullup_dis(gpio_num_t(SNIFF_INV_RX_PIN));
    delay(2000);
    Serial.printf("goodwe-esp sniffer, %d baud, A11 TX on GPIO%d, inverter TX on GPIO%d\n",
                  AA55_BAUD, SNIFF_A11_RX_PIN, SNIFF_INV_RX_PIN);
}

void loop() {
    for (Tap& t : taps) poll(t);
    delay(1);
}
