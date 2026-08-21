#include <unity.h>
#include <cstdint>
#include "drivers/twai_filter.h"

void setUp() {}
void tearDown() {}

void test_wifi_nag_dual_id_hardware_filter_accepts_0x370_and_0x39b()
{
    uint32_t ids[] = {0x370, 0x39B};
    auto f = computeTwaiFilter(ids, 2);
    TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(f, 0x370));
    TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(f, 0x39B));
}

void test_wifi_nag_dual_id_exact_filter_rejects_neighbor_ids()
{
    uint32_t ids[] = {0x370, 0x39B};
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 2, 0x371));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 2, 0x39A));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 2, 0x399));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 2, 0x3FD));
}

void test_ble_filter_preserves_nag_and_observer_ids_and_excludes_0x118()
{
    const uint32_t ids[] = {0x370, 0x39B, 0x255, 0x12B};
    const TwaiFilterResult filter = computeTwaiFilter(ids, 4);
    for (const uint32_t id : ids)
    {
        TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(filter, id));
        TEST_ASSERT_TRUE(exactCanIdMatches(ids, 4, id));
    }
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 4, 0x118));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 4, 0x117));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 4, 0x119));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 4, 0x371));
}

void test_empty_count_returns_zero()
{
    auto f = computeTwaiFilter(nullptr, 0);
    TEST_ASSERT_EQUAL_HEX32(0, f.acceptance_code);
    TEST_ASSERT_EQUAL_HEX32(0, f.acceptance_mask);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_nag_dual_id_hardware_filter_accepts_0x370_and_0x39b);
    RUN_TEST(test_wifi_nag_dual_id_exact_filter_rejects_neighbor_ids);
    RUN_TEST(test_empty_count_returns_zero);
    RUN_TEST(test_ble_filter_preserves_nag_and_observer_ids_and_excludes_0x118);
    return UNITY_END();
}
