#include <unity.h>
#include <cstdint>
#include "drivers/twai_filter.h"
#include "wifi_nag_can_ids.h"

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
    const TwaiFilterResult filter =
        computeTwaiFilter(kWifiNagObservedIds, kWifiNagObservedIdCount);
    TEST_ASSERT_FALSE(filter.single_filter);
    TEST_ASSERT_EQUAL_HEX32(0x25604A00U, filter.acceptance_code);
    TEST_ASSERT_EQUAL_HEX32(0x001F24BFU, filter.acceptance_mask);
    for (const uint32_t id : kWifiNagObservedIds)
    {
        TEST_ASSERT_TRUE(twaiHardwareFilterAccepts(filter, id));
        TEST_ASSERT_TRUE(exactCanIdMatches(kWifiNagObservedIds,
                                           kWifiNagObservedIdCount,
                                           id));
    }
    TEST_ASSERT_FALSE(exactCanIdMatches(kWifiNagObservedIds,
                                        kWifiNagObservedIdCount,
                                        0x118));
}

void test_three_id_filter_has_small_hardware_candidate_set_and_exact_effective_whitelist()
{
    const TwaiFilterResult filter =
        computeTwaiFilter(kWifiNagObservedIds, kWifiNagObservedIdCount);
    uint16_t hardwareCandidateCount = 0;
    uint16_t effectiveCandidateCount = 0;

    for (uint32_t id = 0; id <= 0x7FFU; ++id)
    {
        const bool hardwareAccepted = twaiHardwareFilterAccepts(filter, id);
        const bool exactAccepted = exactCanIdMatches(kWifiNagObservedIds,
                                                     kWifiNagObservedIdCount,
                                                     id);
        if (hardwareAccepted)
            ++hardwareCandidateCount;
        if (hardwareAccepted && exactAccepted)
            ++effectiveCandidateCount;

        if (exactAccepted)
            TEST_ASSERT_TRUE_MESSAGE(hardwareAccepted,
                                     "hardware prefilter dropped a required ID");
    }

    // One exact singleton plus the closest pair represented with four
    // wildcard ID bits: 1 + 2^4 hardware candidates. read() then applies the
    // authoritative exact three-ID software whitelist.
    TEST_ASSERT_EQUAL_UINT16(17, hardwareCandidateCount);
    TEST_ASSERT_EQUAL_UINT16(kWifiNagObservedIdCount, effectiveCandidateCount);
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
    RUN_TEST(test_three_id_filter_has_small_hardware_candidate_set_and_exact_effective_whitelist);
    return UNITY_END();
}
