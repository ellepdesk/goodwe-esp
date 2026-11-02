#include <aa55.h>
#include <string.h>
#include <unity.h>

using namespace aa55;

// Running-info data from a 2023 capture of the HF-A11's Datalog upload (bytes 20..65 of
// the 66-byte body), which is the inverter's AA55 0x01/0x81 reply passed through as-is.
static const uint8_t kCapturedInfo[kRunningInfoLen] = {
    0x07, 0x1c, 0x05, 0x13, 0x00, 0x2f, 0x00, 0x17, 0x09, 0x19, 0x00, 0x2e,
    0x13, 0x8a, 0x04, 0x55, 0x00, 0x01, 0x01, 0x39, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0xda, 0x29, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x39, 0x00, 0x00, 0x00, 0x23,
};

void setUp() {}
void tearDown() {}

static bool feed_all(Parser& p, const uint8_t* bytes, size_t n) {
    bool got = false;
    for (size_t i = 0; i < n; i++) got = p.feed(bytes[i]) || got;
    return got;
}

void test_encode_query_running_info() {
    uint8_t out[kMaxFrame];
    size_t n = encode(0x80, 0x11, kRead, kRunningInfo, nullptr, 0, out, sizeof out);
    const uint8_t expected[] = {0xAA, 0x55, 0x80, 0x11, 0x01, 0x01, 0x00, 0x01, 0x92};
    TEST_ASSERT_EQUAL(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, n);
}

void test_encode_real_power_adjust() {
    uint8_t pct = 50;
    uint8_t out[kMaxFrame];
    size_t n = encode(0x80, 0x11, kExecute, kRealPowerAdjust, &pct, 1, out, sizeof out);
    const uint8_t expected[] = {0xAA, 0x55, 0x80, 0x11, 0x03, 0x1E, 0x01, 0x32, 0x01, 0xE4};
    TEST_ASSERT_EQUAL(sizeof expected, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, n);
}

void test_encode_rejects_small_buffer() {
    uint8_t out[8];
    TEST_ASSERT_EQUAL(0, encode(0x80, 0x11, kRead, kRunningInfo, nullptr, 0, out, sizeof out));
}

void test_roundtrip_with_leading_garbage() {
    uint8_t frame[kMaxFrame];
    size_t n = encode(0x11, 0x80, kRead, kRunningInfo | kResponse, kCapturedInfo,
                      kRunningInfoLen, frame, sizeof frame);
    const uint8_t garbage[] = {0x00, 0xAA, 0x13, 0x55, 0xAA};
    Parser p;
    TEST_ASSERT_FALSE(feed_all(p, garbage, sizeof garbage));
    TEST_ASSERT_TRUE(feed_all(p, frame, n));
    TEST_ASSERT_EQUAL_HEX8(0x11, p.frame().src);
    TEST_ASSERT_EQUAL_HEX8(0x81, p.frame().func);
    TEST_ASSERT_EQUAL(kRunningInfoLen, p.frame().len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCapturedInfo, p.frame().data, kRunningInfoLen);
}

void test_bad_checksum_is_dropped_and_next_frame_parses() {
    uint8_t frame[kMaxFrame];
    size_t n = encode(0x80, 0x11, kRead, kRunningInfo, nullptr, 0, frame, sizeof frame);
    Parser p;
    frame[n - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(feed_all(p, frame, n));
    TEST_ASSERT_EQUAL(1, p.checksum_errors());
    frame[n - 1] ^= 0xFF;
    TEST_ASSERT_TRUE(feed_all(p, frame, n));
}

void test_decode_captured_running_info() {
    RunningInfo info;
    TEST_ASSERT_TRUE(decode_running_info(kCapturedInfo, sizeof kCapturedInfo, info));
    TEST_ASSERT_EQUAL_FLOAT(182.0f, info.vpv1);
    TEST_ASSERT_EQUAL_FLOAT(129.9f, info.vpv2);
    TEST_ASSERT_EQUAL_FLOAT(4.7f, info.ipv1);
    TEST_ASSERT_EQUAL_FLOAT(2.3f, info.ipv2);
    TEST_ASSERT_EQUAL_FLOAT(232.9f, info.vac);
    TEST_ASSERT_EQUAL_FLOAT(4.6f, info.iac);
    TEST_ASSERT_EQUAL_FLOAT(50.02f, info.fac);
    TEST_ASSERT_EQUAL(1109, info.pac);
    TEST_ASSERT_EQUAL(1, info.work_mode);
    TEST_ASSERT_EQUAL_FLOAT(31.3f, info.temperature);
    TEST_ASSERT_EQUAL(0, info.error_code);
    TEST_ASSERT_EQUAL_FLOAT(18692.1f, info.e_total);
    TEST_ASSERT_EQUAL(32764, info.h_total);
    TEST_ASSERT_EQUAL_FLOAT(364.1f, info.vbus);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, info.e_day);
}

void test_decode_rejects_short_payload() {
    RunningInfo info;
    TEST_ASSERT_FALSE(decode_running_info(kCapturedInfo, kRunningInfoLen - 1, info));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_query_running_info);
    RUN_TEST(test_encode_real_power_adjust);
    RUN_TEST(test_encode_rejects_small_buffer);
    RUN_TEST(test_roundtrip_with_leading_garbage);
    RUN_TEST(test_bad_checksum_is_dropped_and_next_frame_parses);
    RUN_TEST(test_decode_captured_running_info);
    RUN_TEST(test_decode_rejects_short_payload);
    return UNITY_END();
}
