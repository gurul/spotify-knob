// Host tests for the device picker state. Run: pio test -e native
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "DevicePicker.h"

namespace {

const char *ID_A = "0d1841b0976bae2a3a310dd74c0f3df354899bc8";
const char *ID_B = "1e2952c1087cbf3b4b421ee85d1f4e0465900cd9";
const char *ID_C = "2f3a63d2198dc04c5c532ff96e205f1576a11dea";

void loadThree(DevicePicker &p, uint32_t now, int activeIndex = 1)
{
    p.open(now);
    p.addDevice(ID_A, "Kitchen", "Speaker", activeIndex == 0, false);
    p.addDevice(ID_B, "MacBook", "Computer", activeIndex == 1, false);
    p.addDevice(ID_C, "Car", "Automobile", activeIndex == 2, true);
    p.finishLoad(true, now);
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---- device list ----------------------------------------------------------

void test_open_starts_loading_and_dirty()
{
    DevicePicker p;
    p.open(100);
    TEST_ASSERT_EQUAL(DevicePicker::State::Loading, p.state());
    TEST_ASSERT_TRUE(p.consumeDirty());
    TEST_ASSERT_FALSE(p.consumeDirty());
}

void test_selection_starts_on_active_device()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    TEST_ASSERT_EQUAL(DevicePicker::State::List, p.state());
    TEST_ASSERT_EQUAL(3, p.count());
    TEST_ASSERT_EQUAL(1, p.selectedIndex());
    TEST_ASSERT_EQUAL_STRING("MacBook", p.selected()->name);
}

void test_no_active_device_selects_first()
{
    DevicePicker p;
    loadThree(p, 0, -1);
    TEST_ASSERT_EQUAL(0, p.selectedIndex());
}

void test_empty_list_is_empty_state()
{
    DevicePicker p;
    p.open(0);
    p.finishLoad(true, 0);
    TEST_ASSERT_EQUAL(DevicePicker::State::Empty, p.state());
}

void test_failed_fetch_is_load_error()
{
    DevicePicker p;
    p.open(0);
    p.finishLoad(false, 0);
    TEST_ASSERT_EQUAL(DevicePicker::State::LoadError, p.state());
}

void test_add_rejects_missing_or_oversized_id()
{
    DevicePicker p;
    p.open(0);
    TEST_ASSERT_FALSE(p.addDevice(nullptr, "x", "Speaker", false, false));
    TEST_ASSERT_FALSE(p.addDevice("", "x", "Speaker", false, false));
    char longId[80];
    memset(longId, 'a', sizeof(longId) - 1);
    longId[sizeof(longId) - 1] = '\0';
    TEST_ASSERT_FALSE(p.addDevice(longId, "x", "Speaker", false, false));
    TEST_ASSERT_EQUAL(0, p.count());
}

void test_add_handles_null_name_and_type()
{
    DevicePicker p;
    p.open(0);
    TEST_ASSERT_TRUE(p.addDevice(ID_A, nullptr, nullptr, false, false));
    TEST_ASSERT_EQUAL_STRING("Unnamed device", p.device(0)->name);
    TEST_ASSERT_EQUAL_STRING("", p.device(0)->type);
}

void test_add_stops_at_capacity()
{
    DevicePicker p;
    p.open(0);
    char id[8];
    for (size_t i = 0; i < DevicePicker::MAX_DEVICES + 3; i++)
    {
        snprintf(id, sizeof(id), "id%u", (unsigned)i);
        p.addDevice(id, "n", "Speaker", false, false);
    }
    TEST_ASSERT_EQUAL(DevicePicker::MAX_DEVICES, p.count());
}

void test_long_utf8_name_is_cut_on_a_character_boundary()
{
    DevicePicker p;
    p.open(0);
    // 40 x "é" (2 bytes each) = 80 bytes, more than the 63-byte name buffer.
    char name[100] = {0};
    for (int i = 0; i < 40; i++)
    {
        strcat(name, "\xC3\xA9");
    }
    TEST_ASSERT_TRUE(p.addDevice(ID_A, name, "Speaker", false, false));
    const char *stored = p.device(0)->name;
    TEST_ASSERT_EQUAL(62, strlen(stored));  // 31 whole characters, not 63 bytes
    TEST_ASSERT_EQUAL_HEX8(0xA9, (unsigned char)stored[61]);
}

// ---- knob -------------------------------------------------------------------

void test_rotate_clamps_at_both_ends()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(+5, 10);
    TEST_ASSERT_EQUAL(2, p.selectedIndex());
    p.rotate(-9, 20);
    TEST_ASSERT_EQUAL(0, p.selectedIndex());
}

void test_rotate_marks_dirty_only_on_change()
{
    DevicePicker p;
    loadThree(p, 0, 0);
    p.consumeDirty();
    p.rotate(-1, 10);
    TEST_ASSERT_FALSE(p.consumeDirty());
    p.rotate(+1, 20);
    TEST_ASSERT_TRUE(p.consumeDirty());
}

void test_press_restricted_is_refused_then_returns_to_list()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(+1, 10);  // Car, restricted
    p.press(20);
    TEST_ASSERT_EQUAL(DevicePicker::State::Refused, p.state());
    TEST_ASSERT_FALSE(p.tick(20 + DevicePicker::RESULT_SHOW_MS - 1));
    TEST_ASSERT_FALSE(p.tick(20 + DevicePicker::RESULT_SHOW_MS));
    TEST_ASSERT_EQUAL(DevicePicker::State::List, p.state());
    TEST_ASSERT_EQUAL(2, p.selectedIndex());
}

void test_press_active_device_closes_without_transfer()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.press(10);
    TEST_ASSERT_EQUAL(DevicePicker::State::Switched, p.state());
    TEST_ASSERT_TRUE(p.tick(10 + DevicePicker::RESULT_SHOW_MS));
}

void test_press_other_device_requests_transfer_to_it()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(-1, 10);
    p.press(20);
    TEST_ASSERT_EQUAL(DevicePicker::State::Switching, p.state());
    TEST_ASSERT_EQUAL_STRING(ID_A, p.targetId());
    TEST_ASSERT_EQUAL_STRING("Kitchen", p.targetName());
    TEST_ASSERT_FALSE(p.tick(999999));  // never times out mid-transfer
}

void test_press_ignored_outside_list()
{
    DevicePicker p;
    p.open(0);
    p.finishLoad(true, 0);  // Empty
    p.press(10);
    TEST_ASSERT_EQUAL(DevicePicker::State::Empty, p.state());
}

// ---- transfer result ------------------------------------------------------------

void test_transfer_ok_closes_after_result_screen()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(-1, 10);
    p.press(20);
    p.finishTransfer(true, true, 30);
    TEST_ASSERT_EQUAL(DevicePicker::State::Switched, p.state());
    TEST_ASSERT_FALSE(p.tick(30 + DevicePicker::RESULT_SHOW_MS - 1));
    TEST_ASSERT_TRUE(p.tick(30 + DevicePicker::RESULT_SHOW_MS));
}

void test_transfer_to_vanished_device_is_gone_then_list()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(-1, 10);  // Kitchen
    p.press(20);
    // Re-fetch after the failure: Kitchen is gone, one device left.
    p.clearDevices();
    p.addDevice(ID_B, "MacBook", "Computer", true, false);
    p.finishTransfer(false, p.contains(p.targetId()), 30);
    TEST_ASSERT_EQUAL(DevicePicker::State::Gone, p.state());
    p.tick(30 + DevicePicker::RESULT_SHOW_MS);
    TEST_ASSERT_EQUAL(DevicePicker::State::List, p.state());
    TEST_ASSERT_EQUAL(1, p.count());
    TEST_ASSERT_EQUAL(0, p.selectedIndex());
}

void test_transfer_failure_with_device_listed_is_failed()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(-1, 10);
    p.press(20);
    p.finishTransfer(false, true, 30);
    TEST_ASSERT_EQUAL(DevicePicker::State::Failed, p.state());
}

void test_gone_with_empty_refetch_goes_to_empty()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.rotate(-1, 10);
    p.press(20);
    p.clearDevices();
    p.finishTransfer(false, false, 30);
    p.tick(30 + DevicePicker::RESULT_SHOW_MS);
    TEST_ASSERT_EQUAL(DevicePicker::State::Empty, p.state());
}

// ---- time and exit ----------------------------------------------------------------

void test_idle_timeout_closes_and_input_restarts_it()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    TEST_ASSERT_FALSE(p.tick(DevicePicker::IDLE_TIMEOUT_MS - 1));
    p.rotate(+1, 5000);
    TEST_ASSERT_FALSE(p.tick(DevicePicker::IDLE_TIMEOUT_MS + 10));
    TEST_ASSERT_TRUE(p.tick(5000 + DevicePicker::IDLE_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(DevicePicker::State::Closed, p.state());
}

void test_idle_timeout_survives_millis_wrap()
{
    DevicePicker p;
    const uint32_t start = 0xFFFFFF00u;
    loadThree(p, start, 1);
    TEST_ASSERT_FALSE(p.tick(start + 1000));  // wraps past zero
    TEST_ASSERT_TRUE(p.tick(start + DevicePicker::IDLE_TIMEOUT_MS));
}

void test_cancel_closes()
{
    DevicePicker p;
    loadThree(p, 0, 1);
    p.cancel();
    TEST_ASSERT_TRUE(p.tick(1));
}

// ---- scrolling window ---------------------------------------------------------------

void test_first_visible_keeps_selection_centered_and_in_range()
{
    DevicePicker p;
    p.open(0);
    char id[8];
    for (int i = 0; i < 9; i++)
    {
        snprintf(id, sizeof(id), "id%d", i);
        p.addDevice(id, "n", "Speaker", i == 0, false);
    }
    p.finishLoad(true, 0);
    TEST_ASSERT_EQUAL(0, p.firstVisible(5));  // selection 0
    p.rotate(+4, 1);
    TEST_ASSERT_EQUAL(2, p.firstVisible(5));  // selection 4 in the middle
    p.rotate(+4, 2);
    TEST_ASSERT_EQUAL(4, p.firstVisible(5));  // selection 8, pinned to the end
    TEST_ASSERT_EQUAL(0, p.firstVisible(20)); // everything fits
}

// ---- text helpers -------------------------------------------------------------------

void test_truncate_short_string_unchanged()
{
    char out[32];
    DevicePicker::truncateUtf8("Kitchen", 20, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Kitchen", out);
}

void test_truncate_ascii_adds_dots()
{
    char out[32];
    DevicePicker::truncateUtf8("Living Room Speaker Group", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Living Roo..", out);
}

void test_truncate_counts_utf8_characters_not_bytes()
{
    char out[32];
    // "Café Ölkammer": 13 characters, 15 bytes.
    DevicePicker::truncateUtf8("Caf\xC3\xA9 \xC3\x96lkammer", 6, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9 \xC3\x96..", out);
}

void test_truncate_respects_small_output_buffer()
{
    char out[6];
    DevicePicker::truncateUtf8("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 10, out, sizeof(out));
    // 5 bytes usable, 2 of them for "..": one whole "é" fits, never a half.
    TEST_ASSERT_EQUAL_STRING("\xC3\xA9..", out);
}

void test_truncate_null_source_gives_empty()
{
    char out[8] = "junk";
    DevicePicker::truncateUtf8(nullptr, 5, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_type_labels()
{
    TEST_ASSERT_EQUAL_STRING("phone", DevicePicker::typeLabel("Smartphone"));
    TEST_ASSERT_EQUAL_STRING("speaker", DevicePicker::typeLabel("Speaker"));
    TEST_ASSERT_EQUAL_STRING("computer", DevicePicker::typeLabel("Computer"));
    TEST_ASSERT_EQUAL_STRING("device", DevicePicker::typeLabel("Toaster"));
    TEST_ASSERT_EQUAL_STRING("device", DevicePicker::typeLabel(nullptr));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_starts_loading_and_dirty);
    RUN_TEST(test_selection_starts_on_active_device);
    RUN_TEST(test_no_active_device_selects_first);
    RUN_TEST(test_empty_list_is_empty_state);
    RUN_TEST(test_failed_fetch_is_load_error);
    RUN_TEST(test_add_rejects_missing_or_oversized_id);
    RUN_TEST(test_add_handles_null_name_and_type);
    RUN_TEST(test_add_stops_at_capacity);
    RUN_TEST(test_long_utf8_name_is_cut_on_a_character_boundary);
    RUN_TEST(test_rotate_clamps_at_both_ends);
    RUN_TEST(test_rotate_marks_dirty_only_on_change);
    RUN_TEST(test_press_restricted_is_refused_then_returns_to_list);
    RUN_TEST(test_press_active_device_closes_without_transfer);
    RUN_TEST(test_press_other_device_requests_transfer_to_it);
    RUN_TEST(test_press_ignored_outside_list);
    RUN_TEST(test_transfer_ok_closes_after_result_screen);
    RUN_TEST(test_transfer_to_vanished_device_is_gone_then_list);
    RUN_TEST(test_transfer_failure_with_device_listed_is_failed);
    RUN_TEST(test_gone_with_empty_refetch_goes_to_empty);
    RUN_TEST(test_idle_timeout_closes_and_input_restarts_it);
    RUN_TEST(test_idle_timeout_survives_millis_wrap);
    RUN_TEST(test_cancel_closes);
    RUN_TEST(test_first_visible_keeps_selection_centered_and_in_range);
    RUN_TEST(test_truncate_short_string_unchanged);
    RUN_TEST(test_truncate_ascii_adds_dots);
    RUN_TEST(test_truncate_counts_utf8_characters_not_bytes);
    RUN_TEST(test_truncate_respects_small_output_buffer);
    RUN_TEST(test_truncate_null_source_gives_empty);
    RUN_TEST(test_type_labels);
    return UNITY_END();
}
