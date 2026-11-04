#include <ha.h>
#include <string.h>
#include <unity.h>

using namespace ha;

static const char kSerial[] = "00000DSU00000000";

// Running-info data from the 2023 capture (see test_aa55).
static const uint8_t kInfo[aa55::kRunningInfoLen] = {
    0x07, 0x1c, 0x05, 0x13, 0x00, 0x2f, 0x00, 0x17, 0x09, 0x19, 0x00, 0x2e,
    0x13, 0x8a, 0x04, 0x55, 0x00, 0x01, 0x01, 0x39, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0xda, 0x29, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x39, 0x00, 0x00, 0x00, 0x23,
};

static aa55::RunningInfo info_in(uint16_t mode) {
    aa55::RunningInfo i;
    aa55::decode_running_info(kInfo, sizeof kInfo, i);
    i.work_mode = mode;
    return i;
}

void setUp() {}
void tearDown() {}

void test_state_json_matches_proxy_fields() {
    std::string json = state_json(kSerial, info_in(1));
    TEST_ASSERT_EQUAL_STRING(
        "{\"device_id\":\"00000DSU00000000\",\"voltage_pv1\":182.0,\"voltage_pv2\":129.9,"
        "\"current_pv1\":4.7,\"current_pv2\":2.3,\"voltage_ac\":232.9,\"current_ac\":4.6,"
        "\"freq_ac\":50.02,\"power_ac\":1109,\"work_mode\":\"Normal\",\"temperature\":31.3,"
        "\"error_code\":0,\"errors\":\"OK\",\"kwh_total\":18692.1,\"hours_total\":32764,"
        "\"voltage_bus\":364.1,\"kwh_daily\":3.5}",
        json.c_str());
}

void test_error_and_mode_names() {
    TEST_ASSERT_EQUAL_STRING("OK", error_names(0).c_str());
    TEST_ASSERT_EQUAL_STRING("Utility Loss, Isolation Failure",
                             error_names((1UL << 9) | (1UL << 18)).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown bit 5", error_names(1UL << 5).c_str());
    TEST_ASSERT_EQUAL_STRING("Permanent Fault", work_mode_name(3));
    TEST_ASSERT_NULL(work_mode_name(4));
    aa55::RunningInfo i = info_in(7);
    TEST_ASSERT_NOT_NULL(strstr(state_json(kSerial, i).c_str(), "\"Unknown (7)\""));
}

void test_discovery_keeps_proxy_ids() {
    Config c;
    std::vector<Message> m = discovery(c, kSerial);
    TEST_ASSERT_EQUAL(28, m.size());  // 16 + 8 detail sensors, removal of 4 control entities
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/goodwe_00000DSU00000000/voltage_pv1/config",
                             m[0].topic.c_str());
    TEST_ASSERT_TRUE(m[0].retain);
    const char* p = m[0].payload.c_str();
    TEST_ASSERT_NOT_NULL(strstr(p, "\"unique_id\":\"voltage_pv1_00000DSU00000000\""));
    TEST_ASSERT_NOT_NULL(
        strstr(p, "\"state_topic\":\"homeassistant/sensor/goodwe_00000DSU00000000/state\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"identifiers\":[\"00000DSU00000000\"]"));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"model\":\"GW-0000-DS\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"expire_after\":600"));
    // Totals have no expiry, so the energy dashboard keeps them overnight.
    TEST_ASSERT_NULL(strstr(m[9].payload.c_str(), "expire_after"));
    TEST_ASSERT_NOT_NULL(strstr(m[14].payload.c_str(), "\"entity_category\":\"diagnostic\""));
    for (size_t i = 24; i < m.size(); i++) TEST_ASSERT_EQUAL_STRING("", m[i].payload.c_str());
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/goodwe_00000DSU00000000/stop/config",
                             m[24].topic.c_str());
    TEST_ASSERT_EQUAL_STRING("homeassistant/number/goodwe_00000DSU00000000/power_limit_w/config",
                             m[27].topic.c_str());
}

void test_discovery_with_power_limit() {
    Config c;
    c.power_limit = true;
    std::vector<Message> m = discovery(c, kSerial);
    // The dummy serial has no rated power, so the watt entity is removed.
    TEST_ASSERT_EQUAL_STRING("", m.back().payload.c_str());
    const Message& n = m[m.size() - 2];
    TEST_ASSERT_EQUAL_STRING("homeassistant/number/goodwe_00000DSU00000000/power_limit/config",
                             n.topic.c_str());
    TEST_ASSERT_NOT_NULL(strstr(
        n.payload.c_str(),
        "\"command_topic\":\"homeassistant/number/goodwe_00000DSU00000000/power_limit/set\""));
    TEST_ASSERT_NOT_NULL(strstr(n.payload.c_str(), "\"retain\":true"));

    m = discovery(c, "13600DSU00000000");
    const Message& w = m.back();
    TEST_ASSERT_EQUAL_STRING("homeassistant/number/goodwe_13600DSU00000000/power_limit_w/config",
                             w.topic.c_str());
    TEST_ASSERT_NOT_NULL(
        strstr(w.payload.c_str(), "\"unique_id\":\"power_limit_w_13600DSU00000000\""));
    TEST_ASSERT_NOT_NULL(strstr(
        w.payload.c_str(),
        "\"command_topic\":\"homeassistant/number/goodwe_13600DSU00000000/power_limit_w/set\""));
    TEST_ASSERT_NOT_NULL(strstr(w.payload.c_str(), "\"max\":3600,"));
    TEST_ASSERT_NOT_NULL(strstr(w.payload.c_str(), "\"unit_of_measurement\":\"W\""));
    TEST_ASSERT_NULL(strstr(w.payload.c_str(), "\"retain\""));
}

void test_stop_start_buttons_are_disabled_by_default() {
    Config c;
    c.power_limit = true;
    std::vector<Message> m = discovery(c, kSerial);
    const Message& stop = m[24];
    TEST_ASSERT_EQUAL_STRING("homeassistant/button/goodwe_00000DSU00000000/stop/config",
                             stop.topic.c_str());
    const char* p = stop.payload.c_str();
    TEST_ASSERT_NOT_NULL(strstr(p, "\"unique_id\":\"stop_00000DSU00000000\""));
    TEST_ASSERT_NOT_NULL(
        strstr(p, "\"command_topic\":\"homeassistant/button/goodwe_00000DSU00000000/stop/press\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"enabled_by_default\":false"));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"entity_category\":\"config\""));
    TEST_ASSERT_NOT_NULL(strstr(m[25].payload.c_str(), "\"unique_id\":\"start_00000DSU00000000\""));
}

void test_detail_sensors() {
    std::vector<Message> m = discovery(Config(), kSerial, "V1.16");
    const Message& fw = m[16];
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/goodwe_00000DSU00000000/firmware/config",
                             fw.topic.c_str());
    const char* p = fw.payload.c_str();
    TEST_ASSERT_NOT_NULL(
        strstr(p, "\"state_topic\":\"homeassistant/sensor/goodwe_00000DSU00000000/details\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"entity_category\":\"diagnostic\""));
    TEST_ASSERT_NOT_NULL(strstr(p, "\"sw_version\":\"V1.16\""));
    TEST_ASSERT_NULL(strstr(p, "expire_after"));
    TEST_ASSERT_NOT_NULL(strstr(m[0].payload.c_str(), "\"sw_version\":\"V1.16\""));
    TEST_ASSERT_NULL(strstr(discovery(Config(), kSerial)[0].payload.c_str(), "sw_version"));

    aa55::IdInfo id{};
    strcpy(id.firmware, "V1.16");
    id.safety_country = 0x14;
    aa55::Settings st{150.0f, 30, 184.0f, 253.0f, 48.03f, 50.97f};
    TEST_ASSERT_EQUAL_STRING(
        "{\"firmware\":\"V1.16\",\"safety_country\":20,\"pv_start_voltage\":150.0,"
        "\"start_delay\":30,\"vac_min\":184.0,\"vac_max\":253.0,\"fac_min\":48.03,"
        "\"fac_max\":50.97}",
        details_json(&id, &st).c_str());
    TEST_ASSERT_EQUAL_STRING("{\"firmware\":\"V1.16\",\"safety_country\":20}",
                             details_json(&id, nullptr).c_str());
    TEST_ASSERT_EQUAL_STRING("{}", details_json(nullptr, nullptr).c_str());
}

void test_rated_power() {
    Config c;
    TEST_ASSERT_EQUAL(3600, rated_power(c, "13600DSU00000000"));
    TEST_ASSERT_EQUAL(0, rated_power(c, kSerial));
    TEST_ASSERT_EQUAL(0, rated_power(c, "1AB00DSU00000000"));
    TEST_ASSERT_EQUAL(0, rated_power(c, "13"));
    c.rated_power = 3000;
    TEST_ASSERT_EQUAL(3000, rated_power(c, "13600DSU00000000"));
}

void test_watts_to_percent_rounds_down() {
    int pct = -1;
    TEST_ASSERT_TRUE(watts_to_percent("1800", 3600, pct));
    TEST_ASSERT_EQUAL(50, pct);
    TEST_ASSERT_TRUE(watts_to_percent("2000", 3600, pct));
    TEST_ASSERT_EQUAL(55, pct);  // 1980 W: never above the request
    TEST_ASSERT_TRUE(watts_to_percent("35.9", 3600, pct));
    TEST_ASSERT_EQUAL(0, pct);
    TEST_ASSERT_TRUE(watts_to_percent("3600", 3600, pct));
    TEST_ASSERT_EQUAL(100, pct);
    TEST_ASSERT_FALSE(watts_to_percent("3601", 3600, pct));
    TEST_ASSERT_FALSE(watts_to_percent("-1", 3600, pct));
    TEST_ASSERT_FALSE(watts_to_percent("1kW", 3600, pct));
    TEST_ASSERT_FALSE(watts_to_percent("100", 0, pct));
}

void test_power_limit_waits_for_normal() {
    PowerLimit p;
    uint8_t pct = 0;
    TEST_ASSERT_TRUE(p.command("50"));
    TEST_ASSERT_FALSE(p.due(pct));  // no running info yet
    p.on_info(info_in(0));
    TEST_ASSERT_FALSE(p.due(pct));  // Wait
    p.on_info(info_in(1));
    TEST_ASSERT_TRUE(p.due(pct));
    TEST_ASSERT_EQUAL(50, pct);
    TEST_ASSERT_FALSE(p.due(pct));  // in flight
    p.on_result(true);
    TEST_ASSERT_EQUAL(50, p.applied());
    p.on_info(info_in(1));
    TEST_ASSERT_FALSE(p.due(pct));  // nothing new
}

void test_power_limit_resent_after_registration_and_wake() {
    PowerLimit p;
    uint8_t pct;
    p.command("30");
    p.on_info(info_in(1));
    TEST_ASSERT_TRUE(p.due(pct));
    p.on_result(true);

    p.on_info(info_in(0));  // evening: back to Wait
    p.on_info(info_in(1));  // morning
    TEST_ASSERT_TRUE(p.due(pct));
    p.on_result(true);

    p.on_registered();
    TEST_ASSERT_EQUAL(-1, p.applied());
    p.on_info(info_in(1));
    TEST_ASSERT_TRUE(p.due(pct));
    TEST_ASSERT_EQUAL(30, pct);
}

void test_power_limit_gives_up_after_max_attempts() {
    PowerLimit p;
    uint8_t pct;
    p.command("10");
    for (int i = 0; i < PowerLimit::kMaxAttempts; i++) {
        p.on_info(info_in(1));
        TEST_ASSERT_TRUE(p.due(pct));
        p.on_result(false);
    }
    p.on_info(info_in(1));
    TEST_ASSERT_FALSE(p.due(pct));
    TEST_ASSERT_EQUAL(-1, p.applied());
}

void test_power_limit_command_during_flight_is_sent_next() {
    PowerLimit p;
    uint8_t pct;
    p.command("20");
    p.on_info(info_in(1));
    TEST_ASSERT_TRUE(p.due(pct));
    p.command("80");
    p.on_result(true);
    TEST_ASSERT_EQUAL(20, p.applied());
    p.on_info(info_in(1));
    TEST_ASSERT_TRUE(p.due(pct));
    TEST_ASSERT_EQUAL(80, pct);
}

void test_power_limit_rejects_bad_payloads() {
    PowerLimit p;
    TEST_ASSERT_FALSE(p.command(""));
    TEST_ASSERT_FALSE(p.command("abc"));
    TEST_ASSERT_FALSE(p.command("101"));
    TEST_ASSERT_FALSE(p.command("-1"));
    TEST_ASSERT_EQUAL(-1, p.wanted());
    TEST_ASSERT_TRUE(p.command("42.0"));
    TEST_ASSERT_EQUAL(42, p.wanted());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_state_json_matches_proxy_fields);
    RUN_TEST(test_error_and_mode_names);
    RUN_TEST(test_discovery_keeps_proxy_ids);
    RUN_TEST(test_discovery_with_power_limit);
    RUN_TEST(test_stop_start_buttons_are_disabled_by_default);
    RUN_TEST(test_detail_sensors);
    RUN_TEST(test_rated_power);
    RUN_TEST(test_watts_to_percent_rounds_down);
    RUN_TEST(test_power_limit_waits_for_normal);
    RUN_TEST(test_power_limit_resent_after_registration_and_wake);
    RUN_TEST(test_power_limit_gives_up_after_max_attempts);
    RUN_TEST(test_power_limit_command_during_flight_is_sent_next);
    RUN_TEST(test_power_limit_rejects_bad_payloads);
    return UNITY_END();
}
