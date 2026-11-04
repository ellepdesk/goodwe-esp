#include <master.h>
#include <string.h>
#include <unity.h>

#include <vector>

using namespace aa55;

static const char kSerial[] = "00000DSU00000000";
static const uint8_t kInv = 0x11;

// Running-info data from the 2023 capture (see test_aa55).
static const uint8_t kInfo[kRunningInfoLen] = {
    0x07, 0x1c, 0x05, 0x13, 0x00, 0x2f, 0x00, 0x17, 0x09, 0x19, 0x00, 0x2e,
    0x13, 0x8a, 0x04, 0x55, 0x00, 0x01, 0x01, 0x39, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0xda, 0x29, 0x00, 0x00, 0x7f, 0xfc, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0e, 0x39, 0x00, 0x00, 0x00, 0x23,
};

// 01/82 and 01/83 replies from the GW3600-DS (2026-10-30), serial replaced.
static const char kIdReply[] =
    "V1.16GW3600-DS                 00000DSU000000003600"
    "\0\0\0\0\0\0\0\0\0\0\0\0\x14";
static const uint8_t kSettingsReply[kSettingsLen] = {0x05, 0xdc, 0x00, 0x1e, 0x07, 0x30,
                                                     0x09, 0xe2, 0x12, 0xc3, 0x13, 0xe9};

struct Harness {
    std::vector<Frame> sent;
    std::vector<uint16_t> pac;
    std::vector<std::pair<uint8_t, bool>> executed;
    std::string registered;
    int details = 0;
    IdInfo id{};
    Settings settings{};
    bool have_id = false, have_settings = false;
    Parser sent_parser;
    Master master;
    uint32_t now = 0;

    Harness()
        : master({
              [this](const uint8_t* b, size_t n) {
                  for (size_t i = 0; i < n; i++)
                      if (sent_parser.feed(b[i])) sent.push_back(sent_parser.frame());
              },
              nullptr,
              [this](const char* s) { registered = s; },
              [this](const RunningInfo& i) { pac.push_back(i.pac); },
              [this](uint8_t f, bool ack) { executed.push_back({f, ack}); },
              [this](const IdInfo* i, const Settings* s) {
                  details++;
                  have_id = i;
                  have_settings = s;
                  if (i) id = *i;
                  if (s) settings = *s;
              },
          }) {}

    void reply(uint8_t src, uint8_t ctrl, uint8_t func, const uint8_t* data, uint8_t len) {
        uint8_t buf[kMaxFrame];
        size_t n = encode(src, Master::kOwnAddress, ctrl, func, data, len, buf, sizeof buf);
        for (size_t i = 0; i < n; i++) master.feed(buf[i], now);
    }

    void advance(uint32_t ms) {
        now += ms;
        master.tick(now);
    }

    const Frame& last() { return sent.back(); }

    void register_inverter() {
        advance(0);
        advance(Master::kRetryMs);
        reply(kBroadcast, kRegister, 0x80, (const uint8_t*)kSerial, 16);
        uint8_t none = 0;
        reply(kInv, kRegister, 0x81, &none, 0);
    }

    void answer_details() {
        advance(1);
        reply(kInv, kRead, 0x82, (const uint8_t*)kIdReply, kIdInfoLen);
        advance(1);
        reply(kInv, kRead, 0x83, kSettingsReply, kSettingsLen);
    }
};

void setUp() {}
void tearDown() {}

void test_registration_sequence() {
    Harness h;
    h.advance(0);
    TEST_ASSERT_EQUAL_HEX8(0x02, h.last().func);  // drop stale registration
    h.advance(Master::kRetryMs);
    TEST_ASSERT_EQUAL_HEX8(kBroadcast, h.last().dst);
    TEST_ASSERT_EQUAL_HEX8(kOfflineQuery, h.last().func);

    h.reply(kBroadcast, kRegister, 0x80, (const uint8_t*)kSerial, 16);
    TEST_ASSERT_EQUAL_HEX8(kAllocateAddress, h.last().func);
    TEST_ASSERT_EQUAL(17, h.last().len);
    TEST_ASSERT_EQUAL_MEMORY(kSerial, h.last().data, 16);
    TEST_ASSERT_EQUAL_HEX8(kInv, h.last().data[16]);

    h.reply(kInv, kRegister, 0x81, nullptr, 0);
    TEST_ASSERT_TRUE(h.master.state() == Master::State::kRunning);
    TEST_ASSERT_EQUAL_STRING(kSerial, h.registered.c_str());
}

void test_reads_details_once_after_registration() {
    Harness h;
    h.register_inverter();
    h.advance(1);
    TEST_ASSERT_EQUAL_HEX8(kRead, h.last().ctrl);
    TEST_ASSERT_EQUAL_HEX8(kIdInfo, h.last().func);
    h.reply(kInv, kRead, 0x82, (const uint8_t*)kIdReply, kIdInfoLen);
    TEST_ASSERT_EQUAL(0, h.details);
    h.advance(1);
    TEST_ASSERT_EQUAL_HEX8(kSettings, h.last().func);
    h.reply(kInv, kRead, 0x83, kSettingsReply, kSettingsLen);
    TEST_ASSERT_EQUAL(1, h.details);
    TEST_ASSERT_TRUE(h.have_id);
    TEST_ASSERT_TRUE(h.have_settings);
    TEST_ASSERT_EQUAL_STRING("V1.16", h.id.firmware);
    TEST_ASSERT_EQUAL_STRING("GW3600-DS", h.id.model);
    TEST_ASSERT_EQUAL_STRING(kSerial, h.id.serial);
    TEST_ASSERT_EQUAL(3600, h.id.nominal);
    TEST_ASSERT_EQUAL_HEX8(0x14, h.id.safety_country);
    TEST_ASSERT_EQUAL_FLOAT(150.0f, h.settings.pv_start_voltage);
    TEST_ASSERT_EQUAL(30, h.settings.start_delay_s);
    TEST_ASSERT_EQUAL_FLOAT(184.0f, h.settings.vac_min);
    TEST_ASSERT_EQUAL_FLOAT(253.0f, h.settings.vac_max);
    TEST_ASSERT_EQUAL_FLOAT(48.03f, h.settings.fac_min);
    TEST_ASSERT_EQUAL_FLOAT(50.97f, h.settings.fac_max);
    h.advance(1);
    TEST_ASSERT_EQUAL_HEX8(kRunningInfo, h.last().func);
    h.reply(kInv, kRead, 0x81, kInfo, sizeof kInfo);
    h.advance(10000);
    TEST_ASSERT_EQUAL_HEX8(kRunningInfo, h.last().func);  // not read again
    TEST_ASSERT_EQUAL(1, h.details);
}

void test_skips_unanswered_details() {
    Harness h;
    h.register_inverter();
    for (int i = 0; i < 2 * Master::kDetailAttempts; i++) {
        h.advance(1);
        TEST_ASSERT_EQUAL_HEX8(i < Master::kDetailAttempts ? kIdInfo : kSettings, h.last().func);
        h.advance(Master::kReplyTimeoutMs);
        h.reply(kInv, kRead, 0x81, kInfo, sizeof kInfo);  // keeps the link alive
    }
    TEST_ASSERT_EQUAL(1, h.details);
    TEST_ASSERT_FALSE(h.have_id);
    TEST_ASSERT_FALSE(h.have_settings);
    TEST_ASSERT_EQUAL_HEX8(kRunningInfo, h.last().func);
}

void test_polls_running_info() {
    Harness h;
    h.register_inverter();
    h.answer_details();
    h.advance(1);
    TEST_ASSERT_EQUAL_HEX8(kRead, h.last().ctrl);
    TEST_ASSERT_EQUAL_HEX8(kRunningInfo, h.last().func);
    TEST_ASSERT_EQUAL_HEX8(kInv, h.last().dst);
    h.reply(kInv, kRead, 0x81, kInfo, sizeof kInfo);
    TEST_ASSERT_EQUAL(1, h.pac.size());
    TEST_ASSERT_EQUAL(1109, h.pac[0]);

    size_t before = h.sent.size();
    h.advance(5000);
    TEST_ASSERT_EQUAL(before, h.sent.size());
    h.advance(5000);
    TEST_ASSERT_EQUAL(before + 1, h.sent.size());
}

void test_execute_ack_and_nak() {
    Harness h;
    h.register_inverter();
    h.answer_details();
    h.advance(1);
    h.reply(kInv, kRead, 0x81, kInfo, sizeof kInfo);

    uint8_t pct = 50;
    TEST_ASSERT_TRUE(h.master.execute(kRealPowerAdjust, &pct, 1));
    TEST_ASSERT_FALSE(h.master.execute(kRealPowerAdjust, &pct, 1));
    h.advance(1);
    TEST_ASSERT_EQUAL_HEX8(kExecute, h.last().ctrl);
    TEST_ASSERT_EQUAL_HEX8(kRealPowerAdjust, h.last().func);
    TEST_ASSERT_EQUAL_HEX8(50, h.last().data[0]);
    uint8_t ack = kAck;
    h.reply(kInv, kExecute, 0x9E, &ack, 1);

    TEST_ASSERT_TRUE(h.master.execute(kStop, nullptr, 0));
    h.advance(1);
    uint8_t nak = kNak;
    h.reply(kInv, kExecute, 0x9C, &nak, 1);

    TEST_ASSERT_EQUAL(2, h.executed.size());
    TEST_ASSERT_EQUAL_HEX8(kRealPowerAdjust, h.executed[0].first);
    TEST_ASSERT_TRUE(h.executed[0].second);
    TEST_ASSERT_EQUAL_HEX8(kStop, h.executed[1].first);
    TEST_ASSERT_FALSE(h.executed[1].second);
}

void test_execute_timeout_reports_failure() {
    Harness h;
    h.register_inverter();
    uint8_t pct = 100;
    h.master.execute(kRealPowerAdjust, &pct, 1);
    h.advance(1);  // a queued command goes out before the first poll
    TEST_ASSERT_EQUAL_HEX8(kExecute, h.last().ctrl);
    h.advance(Master::kReplyTimeoutMs);
    TEST_ASSERT_EQUAL(1, h.executed.size());
    TEST_ASSERT_FALSE(h.executed[0].second);
}

void test_rediscovers_when_inverter_goes_silent() {
    Harness h;
    h.register_inverter();
    for (uint32_t t = 0; t < Master::kOfflineMs; t += 1000) h.advance(1000);
    TEST_ASSERT_TRUE(h.master.state() == Master::State::kDiscover);
}

void test_ignores_own_echo() {
    Harness h;
    h.register_inverter();
    h.answer_details();
    h.advance(1);
    // An RS485 echo of our own poll must not be taken as the reply.
    h.reply(Master::kOwnAddress, kRead, 0x81, kInfo, sizeof kInfo);
    TEST_ASSERT_EQUAL(0, h.pac.size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_registration_sequence);
    RUN_TEST(test_reads_details_once_after_registration);
    RUN_TEST(test_skips_unanswered_details);
    RUN_TEST(test_polls_running_info);
    RUN_TEST(test_execute_ack_and_nak);
    RUN_TEST(test_execute_timeout_reports_failure);
    RUN_TEST(test_rediscovers_when_inverter_goes_silent);
    RUN_TEST(test_ignores_own_echo);
    return UNITY_END();
}
