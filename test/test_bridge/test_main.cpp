#include <bridge.h>
#include <string.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

using namespace ha;

static const char kSerial[] = "13600DSU00000000";
static const std::string kNode = "homeassistant/number/goodwe_13600DSU00000000";
static const std::string kButtons = "homeassistant/button/goodwe_13600DSU00000000";

// Running-info data from the 2023 capture (see test_aa55), in Normal.
static const uint8_t kInfo[aa55::kRunningInfoLen] = {
    0x07, 0x1c, 0x05, 0x13, 0x00, 0x2f, 0x00, 0x17, 0x09, 0x19, 0x00, 0x2e,
    0x13, 0x8a, 0x04, 0x55, 0x00, 0x01, 0x01, 0x39, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0xda, 0x29, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x39, 0x00, 0x00, 0x00, 0x23,
};
static const uint8_t kSettingsReply[aa55::kSettingsLen] = {0x05, 0xdc, 0x00, 0x1e, 0x07, 0x30,
                                                           0x09, 0xe2, 0x12, 0xc3, 0x13, 0xe9};

// A bridge wired to a simulated inverter that answers every request, like fake_inverter.py.
struct Sim {
    std::map<std::string, std::string> retained;
    std::vector<std::pair<std::string, std::string>> published;
    std::vector<std::string> subscribed;
    std::vector<std::string> logs;
    std::vector<aa55::Frame> executed;  // execute frames the inverter received
    std::vector<std::vector<uint8_t>> replies;
    aa55::Parser parser;
    uint8_t address = 0;
    bool off = false;  // answers nothing, as after sunset
    int lose_polls = 0;  // running-info polls to leave unanswered
    uint32_t now = 0;
    Bridge bridge;

    explicit Sim(bool control = true) : bridge(config(control), io()) {}

    static Config config(bool control) {
        Config c;
        c.power_limit = control;
        return c;
    }

    Bridge::Io io() {
        Bridge::Io io;
        io.publish = [this](const std::string& t, const std::string& p, bool retain) {
            published.push_back({t, p});
            if (retain) retained[t] = p;
        };
        io.subscribe = [this](const std::string& t) { subscribed.push_back(t); };
        io.log = [this](const char* line) { logs.push_back(line); };
        io.send = [this](const uint8_t* f, size_t n) {
            for (size_t i = 0; i < n; i++)
                if (parser.feed(f[i])) answer(parser.frame());
        };
        return io;
    }

    void reply(uint8_t src, uint8_t ctrl, uint8_t func, const uint8_t* data, uint8_t len) {
        std::vector<uint8_t> buf(aa55::kMaxFrame);
        buf.resize(aa55::encode(src, 0x80, ctrl, func, data, len, buf.data(), buf.size()));
        replies.push_back(buf);
    }

    void answer(const aa55::Frame& f) {
        if (off) return;
        if (f.ctrl == 0x00 && f.func == 0x00 && !address) {
            reply(0x7F, 0x00, 0x80, (const uint8_t*)kSerial, 16);
        } else if (f.ctrl == 0x00 && f.func == 0x01) {
            address = f.data[16];
            reply(address, 0x00, 0x81, nullptr, 0);
        } else if (f.ctrl == 0x00 && f.func == 0x02) {
            address = 0;
        } else if (f.dst == address && f.ctrl == 0x01 && f.func == 0x01) {
            if (lose_polls > 0 && lose_polls--) return;
            reply(address, 0x01, 0x81, kInfo, sizeof kInfo);
        } else if (f.dst == address && f.ctrl == 0x01 && f.func == 0x02) {
            std::string id = std::string("V1.16GW3600-DS") + std::string(17, ' ') + kSerial +
                             "3600" + std::string(12, '\0') + "\x14";
            reply(address, 0x01, 0x82, (const uint8_t*)id.data(), uint8_t(id.size()));
        } else if (f.dst == address && f.ctrl == 0x01 && f.func == 0x03) {
            reply(address, 0x01, 0x83, kSettingsReply, sizeof kSettingsReply);
        } else if (f.dst == address && f.ctrl == 0x03) {
            executed.push_back(f);
            uint8_t ack = aa55::kAck;
            reply(address, 0x03, f.func | 0x80, &ack, 1);
        }
    }

    // Runs the bridge for ms, delivering queued replies between ticks.
    void run(uint32_t ms) {
        for (uint32_t end = now + ms; now < end; now += 50) {
            bridge.tick(now);
            std::vector<std::vector<uint8_t>> pending;
            pending.swap(replies);
            for (const auto& r : pending) bridge.feed(r.data(), r.size(), now);
        }
    }

    // MQTT delivery, including the echo of retained publishes to our own subscriptions.
    void deliver(const std::string& topic, const std::string& payload) {
        bridge.message(topic, payload);
    }

    size_t logged(const char* prefix) const {
        size_t n = 0;
        for (const auto& l : logs) n += l.compare(0, strlen(prefix), prefix) == 0;
        return n;
    }

    size_t count(const std::string& topic) const {
        size_t n = 0;
        for (const auto& p : published) n += p.first == topic;
        return n;
    }
};

void setUp() {}
void tearDown() {}

void test_registers_and_announces() {
    Sim sim;
    sim.bridge.port_opened();
    sim.bridge.mqtt_connected();
    sim.run(3000);
    TEST_ASSERT_EQUAL_STRING(kSerial, sim.bridge.serial().c_str());
    TEST_ASSERT_EQUAL(4, sim.subscribed.size());
    TEST_ASSERT_TRUE(sim.retained.count(
        "homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config"));
    const std::string& details = sim.retained["homeassistant/sensor/goodwe_13600DSU00000000/details"];
    TEST_ASSERT_NOT_NULL(strstr(details.c_str(), "\"safety_country\":\"NL-A\""));
    // Re-announced with the firmware once the details were read.
    TEST_ASSERT_NOT_NULL(strstr(
        sim.retained["homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config"].c_str(),
        "\"sw_version\":\"V1.16\""));
    TEST_ASSERT_TRUE(sim.count("homeassistant/sensor/goodwe_13600DSU00000000/state") >= 1);
}

void test_power_limit_command_is_sent_and_reported() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.deliver(kNode + "/power_limit/set", "40");
    sim.run(11000);  // the limit waits for fresh running info in Normal
    TEST_ASSERT_EQUAL(1, sim.executed.size());
    TEST_ASSERT_EQUAL_HEX8(aa55::kRealPowerAdjust, sim.executed[0].func);
    TEST_ASSERT_EQUAL(40, sim.executed[0].data[0]);
    TEST_ASSERT_EQUAL_STRING("40", sim.retained[kNode + "/power_limit/state"].c_str());
    TEST_ASSERT_EQUAL_STRING("1440", sim.retained[kNode + "/power_limit_w/state"].c_str());
}

void test_watts_become_a_retained_percentage_command() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.deliver(kNode + "/power_limit_w/set", "2000");
    TEST_ASSERT_EQUAL_STRING("55", sim.retained[kNode + "/power_limit/set"].c_str());
    TEST_ASSERT_EQUAL(0, sim.executed.size());  // only once the % command comes back
    sim.deliver(kNode + "/power_limit/set", "55");
    sim.run(11000);
    TEST_ASSERT_EQUAL(1, sim.executed.size());
    TEST_ASSERT_EQUAL(55, sim.executed[0].data[0]);
}

void test_buttons_send_stop_and_start() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.deliver(kButtons + "/stop/press", "PRESS");
    sim.run(200);
    sim.deliver(kButtons + "/start/press", "PRESS");
    sim.run(200);
    TEST_ASSERT_EQUAL(2, sim.executed.size());
    TEST_ASSERT_EQUAL_HEX8(aa55::kStop, sim.executed[0].func);
    TEST_ASSERT_EQUAL_HEX8(aa55::kStart, sim.executed[1].func);
}

void test_control_off_ignores_commands() {
    Sim sim(false);
    sim.bridge.port_opened();
    sim.run(3000);
    TEST_ASSERT_EQUAL(0, sim.subscribed.size());
    sim.deliver(kNode + "/power_limit/set", "10");
    sim.deliver(kButtons + "/stop/press", "PRESS");
    sim.run(11000);
    TEST_ASSERT_EQUAL(0, sim.executed.size());
    TEST_ASSERT_EQUAL_STRING("", sim.retained[kNode + "/power_limit/config"].c_str());
}

void test_reconnect_reannounces() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    size_t before = sim.count("homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config");
    sim.bridge.mqtt_connected();
    sim.run(100);
    TEST_ASSERT_EQUAL(before + 1,
                      sim.count("homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config"));
    TEST_ASSERT_EQUAL(8, sim.subscribed.size());
}

void test_limit_survives_the_night() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.deliver(kNode + "/power_limit/set", "30");
    sim.run(11000);
    TEST_ASSERT_EQUAL(1, sim.executed.size());

    sim.bridge.port_closed();
    TEST_ASSERT_FALSE(sim.bridge.port_open());
    sim.address = 0;  // the inverter powered off
    sim.run(5000);
    TEST_ASSERT_EQUAL(1, sim.executed.size());

    sim.bridge.port_opened();  // morning
    sim.run(15000);
    TEST_ASSERT_EQUAL(2, sim.executed.size());
    TEST_ASSERT_EQUAL(30, sim.executed[1].data[0]);
}

void test_dropped_registration_is_quiet() {
    Sim sim;
    sim.bridge.port_opened();
    sim.bridge.mqtt_connected();
    sim.run(3000);
    size_t configs = sim.count("homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config");
    size_t states = sim.count("homeassistant/sensor/goodwe_13600DSU00000000/state");
    size_t subscriptions = sim.subscribed.size();

    sim.address = 0;  // the inverter forgets us and ignores polls until registered again
    sim.run(20000);
    TEST_ASSERT_EQUAL(0x11, sim.address);
    // At most one poll period without data, and nothing that makes HA redo the entities.
    TEST_ASSERT_TRUE(sim.count("homeassistant/sensor/goodwe_13600DSU00000000/state") >= states + 1);
    TEST_ASSERT_EQUAL(configs,
                      sim.count("homeassistant/sensor/goodwe_13600DSU00000000/voltage_pv1/config"));
    TEST_ASSERT_EQUAL(subscriptions, sim.subscribed.size());
}

void test_dropped_registration_is_traced() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    TEST_ASSERT_EQUAL(0, sim.logged("trace"));

    sim.address = 0;
    sim.run(20000);
    TEST_ASSERT_EQUAL(1, sim.logged("poll unanswered"));
    TEST_ASSERT_TRUE(sim.logged("trace +") >= 4);  // remove, discovery, its reply, allocation
    TEST_ASSERT_EQUAL(1, sim.logged("trace end: registered"));
    size_t lines = sim.logs.size();
    sim.run(20000);
    TEST_ASSERT_EQUAL(lines, sim.logs.size());  // quiet again once registered
}

void test_trace_stops_when_the_inverter_stays_away() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.off = true;
    sim.run(200000);
    TEST_ASSERT_EQUAL(1, sim.logged("trace end: not registered"));
    size_t lines = sim.logs.size();
    sim.run(60000);
    TEST_ASSERT_EQUAL(lines, sim.logs.size());
}

void test_trace_ends_when_a_lost_poll_is_answered_on_retry() {
    Sim sim;
    sim.bridge.port_opened();
    sim.run(3000);
    sim.lose_polls = 1;
    sim.run(15000);
    TEST_ASSERT_EQUAL(1, sim.logged("poll unanswered"));
    TEST_ASSERT_EQUAL(1, sim.logged("trace end: answered again"));
    TEST_ASSERT_EQUAL(0, sim.logged("inverter stopped answering"));
    size_t lines = sim.logs.size();
    sim.run(30000);
    TEST_ASSERT_EQUAL(lines, sim.logs.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registers_and_announces);
    RUN_TEST(test_power_limit_command_is_sent_and_reported);
    RUN_TEST(test_watts_become_a_retained_percentage_command);
    RUN_TEST(test_buttons_send_stop_and_start);
    RUN_TEST(test_control_off_ignores_commands);
    RUN_TEST(test_reconnect_reannounces);
    RUN_TEST(test_limit_survives_the_night);
    RUN_TEST(test_dropped_registration_is_quiet);
    RUN_TEST(test_dropped_registration_is_traced);
    RUN_TEST(test_trace_stops_when_the_inverter_stays_away);
    RUN_TEST(test_trace_ends_when_a_lost_poll_is_answered_on_retry);
    return UNITY_END();
}
