// ESP32-S3 on the GW3600-DS's USB port: the native USB port runs as host to the
// inverter, WiFi carries MQTT to Home Assistant. Same entities and behaviour as
// tools/host/goodwe-mqtt; the shared logic is ha::Bridge. Logs go to the COM port and to
// MQTT (LOG_TOPIC), updates over the air (ArduinoOTA).

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <bridge.h>

#include "usb_hid.h"

#ifdef STATUS_LED_PIN
#include <status_led.h>
#ifndef STATUS_LED_BRIGHTNESS
#define STATUS_LED_BRIGHTNESS 8  // of 255; 32 is glaring
#endif
#endif

#ifndef WIFI_SSID
#error "Set WIFI_SSID and the other settings in secrets.ini (see secrets.ini.example)"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER nullptr
#define MQTT_PASSWORD nullptr
#endif
#ifndef HOSTNAME
#define HOSTNAME "goodwe-esp"
#endif
#ifndef LOG_TOPIC
#define LOG_TOPIC "goodwe-esp/log"
#endif
#ifndef POLL_SECONDS
#define POLL_SECONDS 10
#endif
#ifndef ALLOW_CONTROL
#define ALLOW_CONTROL 1
#endif

namespace {

constexpr uint32_t kMqttRetryMs = 5000;

WiFiClient net;
PubSubClient mqtt(net);
UsbHid usb;
ha::Bridge* bridge = nullptr;
uint32_t last_mqtt_attempt = 0;
bool ota_started = false;

#ifdef STATUS_LED_PIN
status_led::Status led(STATUS_LED_BRIGHTNESS);
status_led::Rgb led_shown{1, 1, 1};  // forces the first write

void update_led() {
    status_led::Rgb c = led.color(millis());
    if (c.r == led_shown.r && c.g == led_shown.g && c.b == led_shown.b) return;
    neopixelWrite(STATUS_LED_PIN, c.r, c.g, c.b);
    led_shown = c;
}
#endif

void log_line(const char* line) {
    Serial.printf("%10lu %s\n", millis(), line);
    if (mqtt.connected()) mqtt.publish(LOG_TOPIC, line);
}

void on_message(char* topic, uint8_t* payload, unsigned int len) {
    bridge->message(topic, std::string(reinterpret_cast<const char*>(payload), len));
}

void keep_mqtt() {
    if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
    if (millis() - last_mqtt_attempt < kMqttRetryMs) return;
    last_mqtt_attempt = millis();
    if (mqtt.connect(HOSTNAME, MQTT_USER, MQTT_PASSWORD)) {
        log_line("mqtt connected");
        bridge->mqtt_connected();
    } else {
        Serial.printf("mqtt connect failed (%d)\n", mqtt.state());
    }
}

void keep_ota() {
    if (ota_started || WiFi.status() != WL_CONNECTED) return;
    ArduinoOTA.setHostname(HOSTNAME);
#ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
    ArduinoOTA.begin();
    ota_started = true;
    Serial.printf("wifi %s, OTA as %s\n", WiFi.localIP().toString().c_str(), HOSTNAME);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println("goodwe-esp usb");

    ha::Config config;
    config.power_limit = ALLOW_CONTROL;
    ha::Bridge::Io io;
    io.publish = [](const std::string& topic, const std::string& payload, bool retain) {
        mqtt.publish(topic.c_str(), reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size(), retain);
    };
    io.subscribe = [](const std::string& topic) { mqtt.subscribe(topic.c_str(), 1); };
    io.send = [](const uint8_t* f, size_t n) {
        if (!usb.write(f, n)) log_line("usb write failed");
    };
    io.log = log_line;
#ifdef STATUS_LED_PIN
    io.on_info = [](const aa55::RunningInfo& info) { led.on_info(info, millis()); };
#endif
    bridge = new ha::Bridge(config, io, 0x11, POLL_SECONDS * 1000);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setBufferSize(1024);  // discovery configs are up to ~600 bytes
    mqtt.setSocketTimeout(2);  // connect() blocks the loop at most this long
    mqtt.setCallback(on_message);

    if (!usb.begin(0x0084, 0x0041)) log_line("usb host install failed");
}

void loop() {
    keep_ota();
    if (ota_started) ArduinoOTA.handle();
    keep_mqtt();
    mqtt.loop();

    usb.poll();
    if (usb.connected() && !bridge->port_open()) bridge->port_opened();
    if (!usb.connected() && bridge->port_open()) bridge->port_closed();

    uint8_t buf[64];
    size_t n = usb.read(buf, sizeof buf);
    uint32_t now = millis();
    bridge->feed(buf, n, now);
    bridge->tick(now);
#ifdef STATUS_LED_PIN
    update_led();
#endif
    delay(1);
}
