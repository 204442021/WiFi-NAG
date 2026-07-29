#include <unity.h>
#include <cstdint>

// Extracted filter computation logic from TWAIDriver::setFilters() so it can be
// unit-tested without the ESP-IDF TWAI hardware API.
struct TwaiFilterResult
{
    uint32_t acceptance_code;
    uint32_t acceptance_mask;
};

static TwaiFilterResult computeTwaiFilter(const uint32_t *ids, uint8_t count)
{
    TwaiFilterResult r = {};
    if (count == 0)
        return r;

    uint32_t differ = 0;
    for (uint8_t i = 1; i < count; i++)
        differ |= ids[0] ^ ids[i];

    uint32_t base = ids[0] & ~differ;
    r.acceptance_code = base << 21;
    r.acceptance_mask = (differ << 21) | 0x001FFFFF;
    return r;
}

static bool filterAccepts(const TwaiFilterResult &f, uint32_t id)
{
    uint32_t rx = id << 21;
    return (rx & ~f.acceptance_mask) == (f.acceptance_code & ~f.acceptance_mask);
}

void setUp() {}
void tearDown() {}

void test_wifi_nag_single_id_accepts_0x370()
{
    uint32_t ids[] = {880};
    auto f = computeTwaiFilter(ids, 1);
    TEST_ASSERT_TRUE(filterAccepts(f, 880));
}

void test_wifi_nag_single_id_rejects_neighbor_ids()
{
    uint32_t ids[] = {880};
    auto f = computeTwaiFilter(ids, 1);
    TEST_ASSERT_FALSE(filterAccepts(f, 879));
    TEST_ASSERT_FALSE(filterAccepts(f, 881));
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
    return UNITY_END();
}
