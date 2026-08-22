#include <unity.h>

#include "handlers.h"
#include "nag_adaptive_controller.h"

static CanFrame makeDas(uint8_t hos)
{
    CanFrame frame{};
    frame.id = 0x39B;
    frame.dlc = 8;
    frame.data[5] = static_cast<uint8_t>((hos & 0x0F) << 2);
    return frame;
}

static void arm(NagAdaptiveController &controller, uint32_t startMs,
                int16_t torqueCentiNm = 20)
{
    controller.observeEpas(startMs + 1, 0, torqueCentiNm, 0x11111111u);
    controller.observeEpas(startMs + 2, 0, torqueCentiNm, 0x22222222u);
    controller.observeEpas(startMs + 3, 0, torqueCentiNm, 0x33333333u);
}

void setUp() {}
void tearDown() {}

void test_maintenance_default_on_and_legacy_slot_can_turn_it_off()
{
    NagAdaptiveConfig defaults;
    TEST_ASSERT_TRUE(defaults.maintenanceEnabled);

    defaults.torqueDeadbandCentiNm = 0;
    const NagAdaptiveConfig off = NagAdaptiveController::normalizeConfig(defaults);
    TEST_ASSERT_FALSE(off.maintenanceEnabled);

    defaults.torqueDeadbandCentiNm = 5;
    const NagAdaptiveConfig on = NagAdaptiveController::normalizeConfig(defaults);
    TEST_ASSERT_TRUE(on.maintenanceEnabled);
}

void test_maintenance_off_blocks_h0_h2_but_h3_still_corrects()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.torqueDeadbandCentiNm = 0;
    controller.setConfig(config);

    CanFrame h2 = makeDas(2);
    TEST_ASSERT_TRUE(controller.observeDas(h2, 100));
    controller.observeEpas(101, 0, 20, 1);
    controller.observeEpas(102, 0, 20, 2);
    NagAdaptiveDecision normal = controller.observeEpas(103, 0, 20, 3);
    TEST_ASSERT_FALSE(normal.shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_MAINTENANCE_DISABLED,
                            controller.snapshot(103).blockReason);

    CanFrame h3 = makeDas(3);
    TEST_ASSERT_TRUE(controller.observeDas(h3, 104));
    NagAdaptiveDecision corrective = controller.observeEpas(105, 0, 20, 4);
    TEST_ASSERT_TRUE(corrective.shouldSend);
    TEST_ASSERT_TRUE(corrective.corrective);
    TEST_ASSERT_TRUE(corrective.targetTorqueCentiNm <= -180);
    TEST_ASSERT_TRUE(corrective.targetTorqueCentiNm >= -200);
}

void test_direction_flips_immediately_without_deadband_or_100ms_confirmation()
{
    NagAdaptiveController controller;
    controller.setConfig(NagAdaptiveConfig{});
    CanFrame h3 = makeDas(3);
    TEST_ASSERT_TRUE(controller.observeDas(h3, 100));
    arm(controller, 100, 20);

    NagAdaptiveDecision negative = controller.observeEpas(104, 0, 1, 5);
    TEST_ASSERT_TRUE(negative.shouldSend);
    TEST_ASSERT_EQUAL_INT8(-1, negative.injectionSign);

    NagAdaptiveDecision positive = controller.observeEpas(105, 0, -1, 6);
    TEST_ASSERT_TRUE(positive.shouldSend);
    TEST_ASSERT_EQUAL_INT8(1, positive.injectionSign);
}

void test_zero_torque_holds_last_direction()
{
    NagAdaptiveController controller;
    controller.setConfig(NagAdaptiveConfig{});
    CanFrame h3 = makeDas(3);
    TEST_ASSERT_TRUE(controller.observeDas(h3, 100));
    arm(controller, 100, 20);

    NagAdaptiveDecision first = controller.observeEpas(104, 0, 20, 7);
    TEST_ASSERT_EQUAL_INT8(-1, first.injectionSign);
    NagAdaptiveDecision zero = controller.observeEpas(105, 0, 0, 8);
    TEST_ASSERT_TRUE(zero.shouldSend);
    TEST_ASSERT_EQUAL_INT8(-1, zero.injectionSign);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::DIRECTION_HOLD,
                            controller.snapshot(105).directionSource);
}

void test_corrective_range_is_shared_and_can_reach_two_nm()
{
    NagAdaptiveConfig config;
    config.correctiveNegativeMinCentiNm = 200;
    config.correctiveNegativeMaxCentiNm = 200;
    config.correctivePositiveMinCentiNm = 200;
    config.correctivePositiveMaxCentiNm = 200;

    NagAdaptiveController controller;
    controller.setConfig(config);
    CanFrame h3 = makeDas(3);
    TEST_ASSERT_TRUE(controller.observeDas(h3, 100));
    arm(controller, 100, 20);
    NagAdaptiveDecision negative = controller.observeEpas(104, 0, 20, 9);
    TEST_ASSERT_EQUAL_INT16(-200, negative.targetTorqueCentiNm);

    NagAdaptiveDecision positive = controller.observeEpas(105, 0, -20, 10);
    TEST_ASSERT_EQUAL_INT16(200, positive.targetTorqueCentiNm);
}

void test_adaptive_encoder_reaches_two_nm_while_legacy_encoder_stays_1_8_nm()
{
    TEST_ASSERT_EQUAL_UINT16(2250, NagHandler::centiNmToAdaptiveRaw(200));
    TEST_ASSERT_EQUAL_UINT16(1850, NagHandler::centiNmToAdaptiveRaw(-200));
    TEST_ASSERT_EQUAL_UINT16(2230, NagHandler::centiNmToRaw(200));
    TEST_ASSERT_EQUAL_UINT16(1870, NagHandler::centiNmToRaw(-200));
    TEST_ASSERT_EQUAL_INT16(180, NagHandler().targetTorqueCentiNm());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_maintenance_default_on_and_legacy_slot_can_turn_it_off);
    RUN_TEST(test_maintenance_off_blocks_h0_h2_but_h3_still_corrects);
    RUN_TEST(test_direction_flips_immediately_without_deadband_or_100ms_confirmation);
    RUN_TEST(test_zero_torque_holds_last_direction);
    RUN_TEST(test_corrective_range_is_shared_and_can_reach_two_nm);
    RUN_TEST(test_adaptive_encoder_reaches_two_nm_while_legacy_encoder_stays_1_8_nm);
    return UNITY_END();
}
