#include <unity.h>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "handlers.h"
#include "drivers/mock_driver.h"

static MockDriver mock;
static NagHandler handler;

// Helper: build a realistic CAN 880 frame
static CanFrame makeEpasFrame(uint8_t handsOn, float torqueNm, uint8_t counter, uint8_t eacStatus = 2)
{
    CanFrame f = {.id = 880, .dlc = 8};
    // bytes 0-1: steeringRackForce (arbitrary realistic values)
    f.data[0] = 0x12;
    f.data[1] = 0x00;
    // bytes 2-3: torsionBarTorque = (torque + 20.5) / 0.01
    uint16_t tRaw = static_cast<uint16_t>((torqueNm + 20.5) / 0.01);
    f.data[2] = 0x08 | ((tRaw >> 8) & 0x0F); // upper nibble = flags (0x08)
    f.data[3] = tRaw & 0xFF;
    // byte 4: handsOnLevel in bits 7:6, internalSAS bits in lower
    f.data[4] = static_cast<uint8_t>((handsOn & 0x03) << 6) | 0x1F;
    // byte 5: internalSAS LSB
    f.data[5] = 0x89;
    // byte 6: upper nibble = eacStatus/tireID, lower nibble = counter
    f.data[6] = static_cast<uint8_t>((eacStatus << 5) | (counter & 0x0F));
    // byte 7: checksum = sum(b0..b6) + 0x73
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return f;
}

// Helper: verify checksum of a frame
static bool verifyChecksum(const CanFrame &f)
{
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    return f.data[7] == static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static float decodeTorqueNm(const CanFrame &f)
{
    uint16_t tRaw = ((f.data[2] & 0x0F) << 8) | f.data[3];
    return tRaw * 0.01f - 20.5f;
}

void setUp()
{
    mock.reset();
    handler = NagHandler();
    handler.enablePrint = false;
    // Existing fixed-A behavior tests run inside an explicit BLE-style window.
    handler.setTestNowMs(0);
    handler.triggerAModeWindow(60000);
}

void tearDown() {}

// ============================================================
// Filter IDs
// ============================================================

void test_nag_filter_ids_count()
{
    TEST_ASSERT_EQUAL_UINT8(1, handler.filterIdCount());
}

void test_nag_filter_ids_value()
{
    const uint32_t *ids = handler.filterIds();
    TEST_ASSERT_EQUAL_UINT32(880, ids[0]);
}

void test_nag_av2_default_range_is_1_50_to_1_80_nm()
{
    TEST_ASSERT_EQUAL_INT16(150, handler.av2MinCenti());
    TEST_ASSERT_EQUAL_INT16(180, handler.av2MaxCenti());
}

// ============================================================
// Basic echo behavior
// ============================================================

void test_nag_a_mode_is_idle_without_ble_window()
{
    NagHandler idleHandler;
    MockDriver idleMock;
    idleHandler.enablePrint = false;
    idleHandler.setTestNowMs(0);

    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    NagHandler::writeTorqueRaw(f, NagHandler::centiNmToRaw(33));
    idleHandler.handleMessage(f, idleMock);

    TEST_ASSERT_EQUAL(0, idleMock.sent.size());
    TEST_ASSERT_FALSE(idleHandler.aModeActive());
    TEST_ASSERT_EQUAL_INT16(33, idleHandler.lastObservedCenti());
}

void test_nag_echoes_when_handson_0()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_nag_echoes_when_handson_1()
{
    CanFrame f = makeEpasFrame(1, 1.5, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_nag_echoes_when_handson_2()
{
    CanFrame f = makeEpasFrame(2, 2.5, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_nag_echoes_when_handson_3()
{
    CanFrame f = makeEpasFrame(3, 3.0, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_nag_does_not_echo_when_disabled()
{
    handler.nagKillerActive = false;
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_tracks_live_torque_even_when_disabled()
{
    handler.nagKillerActive = false;
    CanFrame f = makeEpasFrame(0, -0.80, 0x0C);
    NagHandler::writeTorqueRaw(f, NagHandler::centiNmToRaw(-80));
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_INT16(-80, handler.lastObservedCenti());
    TEST_ASSERT_FLOAT_WITHIN(0.01, -0.80, handler.lastObservedNm());
}

void test_nag_ignores_non_880_id()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.id = 881; // wrong ID
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_nag_ignores_short_dlc()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.dlc = 7; // too short
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

// ============================================================
// Counter+1 logic
// ============================================================

void test_nag_counter_increments_by_1()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t outCounter = mock.sent[0].data[6] & 0x0F;
    TEST_ASSERT_EQUAL_HEX8(0x0D, outCounter); // 0x0C + 1
}

void test_nag_counter_wraps_from_f_to_0()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0F);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t outCounter = mock.sent[0].data[6] & 0x0F;
    TEST_ASSERT_EQUAL_HEX8(0x00, outCounter); // 0x0F + 1 wraps to 0
}

void test_nag_counter_preserves_upper_nibble()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x05, 2); // eacStatus=2 -> upper nibble = 0x40
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t upperNibble = mock.sent[0].data[6] & 0xF0;
    uint8_t expectedUpper = f.data[6] & 0xF0;
    TEST_ASSERT_EQUAL_HEX8(expectedUpper, upperNibble);
}

// ============================================================
// Modified field values
// ============================================================

void test_nag_sets_handson_to_1()
{
    CanFrame f = makeEpasFrame(2, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    uint8_t outHandsOn = (mock.sent[0].data[4] >> 6) & 0x03;
    TEST_ASSERT_EQUAL_UINT8(1, outHandsOn);
}

void test_nag_preserves_byte4_lower_bits()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.data[4] = 0x1F; // handsOn=0, lower bits = 0x1F
    handler.handleMessage(f, mock);
    uint8_t outLower = mock.sent[0].data[4] & 0x3F;
    TEST_ASSERT_EQUAL_HEX8(0x1F, outLower); // lower 6 bits preserved
}

void test_nag_sets_fixed_torque_0xB6()
{
    CanFrame f = makeEpasFrame(0, 0.10, 0x0C); // low torque
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xB6, mock.sent[0].data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x08, mock.sent[0].data[2] & 0x0F);
}

void test_nag_torque_value_is_1_80_nm()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.80, decodeTorqueNm(mock.sent[0]));
}

void test_nag_copies_bytes_0_1_2_5_unchanged()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    f.data[0] = 0xAB;
    f.data[1] = 0xCD;
    f.data[2] = 0x8E; // upper nibble has flags
    f.data[5] = 0x42;
    // Recompute checksum after manual changes
    uint16_t sum = 0;
    for (int i = 0; i < 7; i++)
        sum += f.data[i];
    f.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_HEX8(0xAB, mock.sent[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, mock.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x88, mock.sent[0].data[2]); // upper nibble preserved, lower nibble = 0x08 (fixed torque 0x08B6)
    TEST_ASSERT_EQUAL_HEX8(0x42, mock.sent[0].data[5]);
}

// ============================================================
// Checksum verification
// ============================================================

void test_nag_checksum_correct()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_checksum_correct_at_counter_boundary()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0F); // counter wraps
    handler.handleMessage(f, mock);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_checksum_correct_with_various_inputs()
{
    // Test across multiple counter values and torques
    for (uint8_t cnt = 0; cnt < 16; cnt++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, -5.0 + cnt * 0.7, cnt);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        TEST_ASSERT_TRUE_MESSAGE(verifyChecksum(mock.sent[0]), "Checksum failed for counter sweep");
    }
}

// ============================================================
// Canary: output torque must stay in safe range
// ============================================================

void test_nag_output_torque_never_exceeds_safe_range()
{
    // The fixed torque is 1.80 Nm. Verify it's always in [-5, 5] Nm range.
    for (uint8_t cnt = 0; cnt < 16; cnt++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(0, -20.0 + cnt * 2.5, cnt);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());

        float torque = decodeTorqueNm(mock.sent[0]);

        // Must be exactly 1.80 Nm (from fixed byte 3 = 0xB6)
        TEST_ASSERT_FLOAT_WITHIN(0.01, 1.80, torque);
        // Must never exceed safe range
        TEST_ASSERT_TRUE(torque >= -5.0f);
        TEST_ASSERT_TRUE(torque <= 5.0f);
    }
}

// ============================================================
// A V2 mode
// ============================================================

void test_nag_av2_random_sweep_starts_immediately()
{
    handler.setTestNowMs(0);
    handler.setMode(NagHandler::MODE_A_V2);
    handler.setAv2RangeNm(-1.50f, 0.0f);

    CanFrame f = makeEpasFrame(0, 0.33, 0x02);
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    const float torque = decodeTorqueNm(mock.sent[0]);
    TEST_ASSERT_TRUE(torque >= -1.50f);
    TEST_ASSERT_TRUE(torque <= 0.0f);
    TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
}

void test_nag_av2_random_sweep_updates_over_2000ms_period()
{
    handler.setTestNowMs(100);
    handler.setMode(NagHandler::MODE_A_V2);
    handler.setAv2RangeNm(-1.50f, 0.0f);

    const uint32_t start = 100;
    const uint32_t times[] = {start, start + 500, start + 1000, start + 2000};
    float observed[4] = {};

    for (int i = 0; i < 4; i++)
    {
        mock.reset();
        handler.setTestNowMs(times[i]);
        CanFrame f = makeEpasFrame(0, 0.33, static_cast<uint8_t>(i));
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        observed[i] = decodeTorqueNm(mock.sent[0]);
        TEST_ASSERT_TRUE(observed[i] >= -1.50f);
        TEST_ASSERT_TRUE(observed[i] <= 0.0f);
        TEST_ASSERT_TRUE(verifyChecksum(mock.sent[0]));
    }
    TEST_ASSERT_TRUE(observed[0] != observed[1] || observed[1] != observed[2] || observed[2] != observed[3]);
}

void test_nag_av2_clamps_and_swaps_range()
{
    handler.setAv2RangeNm(2.50f, -2.25f);
    TEST_ASSERT_EQUAL_INT16(-180, handler.av2MinCenti());
    TEST_ASSERT_EQUAL_INT16(180, handler.av2MaxCenti());
}

void test_nag_av2_negative_endpoint_encoding()
{
    handler.setTestNowMs(0);
    handler.setMode(NagHandler::MODE_A_V2);
    handler.setAv2RangeNm(-1.80f, -1.80f);

    CanFrame f = makeEpasFrame(0, 0.33, 0x01);
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x07, mock.sent[0].data[2] & 0x0F);
    TEST_ASSERT_EQUAL_HEX8(0x4E, mock.sent[0].data[3]);
    TEST_ASSERT_FLOAT_WITHIN(0.01, -1.80, decodeTorqueNm(mock.sent[0]));
}

void test_nag_av2_echoes_and_sets_handson_1_frame()
{
    handler.setTestNowMs(0);
    handler.setMode(NagHandler::MODE_A_V2);

    CanFrame f = makeEpasFrame(3, 0.33, 0x03);
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    uint8_t outHandsOn = (mock.sent[0].data[4] >> 6) & 0x03;
    TEST_ASSERT_EQUAL_UINT8(1, outHandsOn);
}

void test_nag_av2_skips_own_echo()
{
    handler.setTestNowMs(0);
    handler.setMode(NagHandler::MODE_A_V2);

    CanFrame f = makeEpasFrame(0, 0.33, 0x03);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    CanFrame ownEcho = mock.sent[0];
    handler.handleMessage(ownEcho, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagOwnEchoSkipCount);
}

void test_nag_a_skips_own_echo()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x03);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    CanFrame ownEcho = mock.sent[0];
    handler.handleMessage(ownEcho, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagOwnEchoSkipCount);
}

void test_nag_output_sets_each_handson_level_to_1()
{
    for (uint8_t ho = 0; ho < 4; ho++)
    {
        mock.reset();
        CanFrame f = makeEpasFrame(ho, 0.33, 0x0C);
        handler.handleMessage(f, mock);
        TEST_ASSERT_EQUAL(1, mock.sent.size());
        uint8_t out = (mock.sent[0].data[4] >> 6) & 0x03;
        TEST_ASSERT_EQUAL_UINT8(1, out);
    }
}

// ============================================================
// Frame count tracking
// ============================================================

void test_nag_increments_frames_sent()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
}

void test_nag_increments_echo_count()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagEchoCount);
}

void test_nag_multiple_frames_count_correctly()
{
    for (int i = 0; i < 10; i++)
    {
        CanFrame f = makeEpasFrame(0, 0.33, i & 0x0F);
        handler.handleMessage(f, mock);
    }
    TEST_ASSERT_EQUAL_UINT32(10, handler.nagEchoCount);
    TEST_ASSERT_EQUAL(10, mock.sent.size());
}

void test_nag_failed_send_is_counted_without_arming_own_echo()
{
    mock.sendResult = false;
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagTxDropCount);
    TEST_ASSERT_FALSE(handler.hasLastInjected);
    TEST_ASSERT_EQUAL_INT16(0, handler.lastInjectedCenti());
}

// ============================================================
// Edge case: mixed handsOn sequence
// ============================================================

void test_nag_echoes_all_handson_levels_in_mixed_sequence()
{
    // Simulate: ho=0, ho=1, ho=0, ho=2, ho=0
    CanFrame f0a = makeEpasFrame(0, 0.33, 0x00);
    CanFrame f1 = makeEpasFrame(1, 1.50, 0x01);
    CanFrame f0b = makeEpasFrame(0, 0.10, 0x02);
    CanFrame f2 = makeEpasFrame(2, 2.50, 0x03);
    CanFrame f0c = makeEpasFrame(0, 0.05, 0x04);

    handler.handleMessage(f0a, mock);
    handler.handleMessage(f1, mock);
    handler.handleMessage(f0b, mock);
    handler.handleMessage(f2, mock);
    handler.handleMessage(f0c, mock);

    TEST_ASSERT_EQUAL(5, mock.sent.size());
}

// ============================================================
// Output ID is always 880
// ============================================================

void test_nag_output_id_is_880()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT32(880, mock.sent[0].id);
}

void test_nag_output_dlc_is_8()
{
    CanFrame f = makeEpasFrame(0, 0.33, 0x0C);
    handler.handleMessage(f, mock);
    TEST_ASSERT_EQUAL_UINT8(8, mock.sent[0].dlc);
}

void test_nag_a_mode_window_forces_mode_a_and_expires()
{
    handler.cancelAModeWindow();
    handler.setTestNowMs(1000);
    handler.setMode(NagHandler::MODE_A_V2);
    TEST_ASSERT_EQUAL_UINT8(NagHandler::MODE_A_V2, (uint8_t)handler.nagMode);
    TEST_ASSERT_FALSE(handler.aModeActive());

    handler.triggerAModeWindow(10000);
    TEST_ASSERT_EQUAL_UINT8(NagHandler::MODE_A, (uint8_t)handler.nagMode);
    TEST_ASSERT_TRUE(handler.aModeActive());
    TEST_ASSERT_EQUAL_UINT32(10000, handler.aModeRemainingMs());

    handler.setTestNowMs(6000);
    TEST_ASSERT_TRUE(handler.aModeActive());
    TEST_ASSERT_EQUAL_UINT32(5000, handler.aModeRemainingMs());
    CanFrame activeFrame = makeEpasFrame(0, 0.33, 0x01);
    handler.handleMessage(activeFrame, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1.80, decodeTorqueNm(mock.sent[0]));

    handler.setTestNowMs(11000);
    TEST_ASSERT_FALSE(handler.aModeActive());
    TEST_ASSERT_EQUAL_UINT32(0, handler.aModeRemainingMs());
    mock.reset();
    CanFrame expiredFrame = makeEpasFrame(0, 0.33, 0x02);
    handler.handleMessage(expiredFrame, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

int main()
{
    UNITY_BEGIN();

    // Filter
    RUN_TEST(test_nag_filter_ids_count);
    RUN_TEST(test_nag_filter_ids_value);
    RUN_TEST(test_nag_av2_default_range_is_1_50_to_1_80_nm);

    // Basic echo behavior
    RUN_TEST(test_nag_a_mode_is_idle_without_ble_window);
    RUN_TEST(test_nag_echoes_when_handson_0);
    RUN_TEST(test_nag_echoes_when_handson_1);
    RUN_TEST(test_nag_echoes_when_handson_2);
    RUN_TEST(test_nag_echoes_when_handson_3);
    RUN_TEST(test_nag_does_not_echo_when_disabled);
    RUN_TEST(test_nag_tracks_live_torque_even_when_disabled);
    RUN_TEST(test_nag_ignores_non_880_id);
    RUN_TEST(test_nag_ignores_short_dlc);

    // Counter+1
    RUN_TEST(test_nag_counter_increments_by_1);
    RUN_TEST(test_nag_counter_wraps_from_f_to_0);
    RUN_TEST(test_nag_counter_preserves_upper_nibble);

    // Modified fields
    RUN_TEST(test_nag_sets_handson_to_1);
    RUN_TEST(test_nag_preserves_byte4_lower_bits);
    RUN_TEST(test_nag_sets_fixed_torque_0xB6);
    RUN_TEST(test_nag_torque_value_is_1_80_nm);
    RUN_TEST(test_nag_copies_bytes_0_1_2_5_unchanged);

    // Checksum
    RUN_TEST(test_nag_checksum_correct);
    RUN_TEST(test_nag_checksum_correct_at_counter_boundary);
    RUN_TEST(test_nag_checksum_correct_with_various_inputs);

    // Safety canary
    RUN_TEST(test_nag_output_torque_never_exceeds_safe_range);
    RUN_TEST(test_nag_output_sets_each_handson_level_to_1);

    // A V2
    RUN_TEST(test_nag_av2_random_sweep_starts_immediately);
    RUN_TEST(test_nag_av2_random_sweep_updates_over_2000ms_period);
    RUN_TEST(test_nag_av2_clamps_and_swaps_range);
    RUN_TEST(test_nag_av2_negative_endpoint_encoding);
    RUN_TEST(test_nag_av2_echoes_and_sets_handson_1_frame);
    RUN_TEST(test_nag_av2_skips_own_echo);
    RUN_TEST(test_nag_a_skips_own_echo);

    // Counters
    RUN_TEST(test_nag_increments_frames_sent);
    RUN_TEST(test_nag_increments_echo_count);
    RUN_TEST(test_nag_multiple_frames_count_correctly);
    RUN_TEST(test_nag_failed_send_is_counted_without_arming_own_echo);

    // Edge cases
    RUN_TEST(test_nag_echoes_all_handson_levels_in_mixed_sequence);

    // Output frame
    RUN_TEST(test_nag_output_id_is_880);
    RUN_TEST(test_nag_output_dlc_is_8);

    // BLE-triggered A-mode display window
    RUN_TEST(test_nag_a_mode_window_forces_mode_a_and_expires);

    return UNITY_END();
}
