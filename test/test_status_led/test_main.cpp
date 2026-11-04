#include <status_led.h>
#include <unity.h>

using namespace status_led;

static aa55::RunningInfo info(uint16_t mode, uint32_t errors = 0) {
    aa55::RunningInfo i{};
    i.work_mode = mode;
    i.error_code = errors;
    return i;
}

static void assert_rgb(Rgb want, Rgb got) {
    TEST_ASSERT_EQUAL(want.r, got.r);
    TEST_ASSERT_EQUAL(want.g, got.g);
    TEST_ASSERT_EQUAL(want.b, got.b);
}

void setUp() {}
void tearDown() {}

void test_colors_per_mode() {
    assert_rgb({255, 80, 0}, color_for(info(0)));
    assert_rgb({0, 255, 0}, color_for(info(1)));
    assert_rgb({255, 0, 0}, color_for(info(2)));
    assert_rgb({255, 0, 0}, color_for(info(3)));
    assert_rgb({255, 0, 0}, color_for(info(7)));
}

void test_error_bit_is_red_in_any_mode() {
    assert_rgb({255, 0, 0}, color_for(info(1, 1UL << 12)));
    assert_rgb({255, 0, 0}, color_for(info(0, 1)));
}

void test_off_until_info_and_when_stale() {
    Status s(255);
    assert_rgb(kOff, s.color(0));
    s.on_info(info(1), 1000);
    assert_rgb({0, 255, 0}, s.color(1000 + kStaleMs));
    assert_rgb(kOff, s.color(1001 + kStaleMs));
}

void test_brightness_scales() {
    Status s(32);
    s.on_info(info(0), 0);
    assert_rgb({32, 10, 0}, s.color(0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_colors_per_mode);
    RUN_TEST(test_error_bit_is_red_in_any_mode);
    RUN_TEST(test_off_until_info_and_when_stale);
    RUN_TEST(test_brightness_scales);
    return UNITY_END();
}
