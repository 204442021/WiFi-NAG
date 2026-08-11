#include <unity.h>
#include <cstdint>
#include "drivers/twai_filter.h"

void setUp() {}
void tearDown() {}

void test_wifi_nag_single_id_accepts_0x370()
{
    uint32_t ids[] = {880};
    auto f = computeTwaiFilter(ids, 1);
    TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(f, 880));
}

void test_wifi_nag_single_id_rejects_neighbor_ids()
{
    uint32_t ids[] = {880};
    auto f = computeTwaiFilter(ids, 1);
    TEST_ASSERT_FALSE(twaiHardwareFilterAccepts(f, 879));
    TEST_ASSERT_FALSE(twaiHardwareFilterAccepts(f, 881));
}

void test_ble_filter_contains_three_required_ids_and_excludes_0x118()
{
    const uint32_t ids[] = {0x370, 0x255, 0x12B};
    const TwaiFilterResult filter = computeTwaiFilter(ids, 3);
    for (const uint32_t id : ids)
    {
        TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(filter, id));
        TEST_ASSERT_TRUE(exactCanIdMatches(ids, 3, id));
    }
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 3, 0x118));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 3, 0x117));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 3, 0x119));
    TEST_ASSERT_FALSE(exactCanIdMatches(ids, 3, 0x371));
}

void test_wifi_nag_single_id_mask_is_exact()
{
    uint32_t ids[] = {880};
    auto f = computeTwaiFilter(ids, 1);
    TEST_ASSERT_EQUAL_HEX32(880u << 21, f.acceptance_code);
    TEST_ASSERT_EQUAL_HEX32(0x001FFFFF, f.acceptance_mask);
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
    RUN_TEST(test_wifi_nag_single_id_accepts_0x370);
    RUN_TEST(test_wifi_nag_single_id_rejects_neighbor_ids);
    RUN_TEST(test_wifi_nag_single_id_mask_is_exact);
    RUN_TEST(test_empty_count_returns_zero);
    RUN_TEST(test_ble_filter_contains_three_required_ids_and_excludes_0x118);
    return UNITY_END();
}
