#pragma once

#if defined(ESP_PLATFORM) && defined(DASH_STA_AP_GATEWAY)

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <nvs.h>
#include <apps/dhcpserver/dhcpserver.h>
#include <lwip/inet.h>
#include <lwip/lwip_napt.h>
#include <lwip/netif.h>
#include <fcntl.h>
#include <lwip/sockets.h>

static constexpr const char *kDashGatewayTag = "dash_gateway";
static constexpr const char *kDashGatewayPrefsNs = "gw";
static constexpr const char *kDashGatewayBlacklistPath = "/gw_black.txt";
static constexpr const char *kDashGatewayWhitelistPath = "/gw_white.txt";
static constexpr const char *kDashGatewayCidrPath = "/gw_cidr.txt";
static constexpr uint8_t kDashGatewayTeslaProfileVersion = 1;
static constexpr size_t kDashGatewayListMax = 3072;
static constexpr size_t kDashGatewayCidrListMax = 1536;
static constexpr uint16_t kDashGatewayDnsTypeA = 1;
static constexpr uint16_t kDashGatewayDnsTypeAAAA = 28;
static constexpr uint16_t kDashGatewayDnsClassIN = 1;
static constexpr uint32_t kDashGatewayBlockedTtlSeconds = 60;
static constexpr size_t kDashGatewayMaxPending = 64;
static constexpr uint8_t kDashGatewayMaxPendingClients = 4;
static constexpr size_t kDashGatewayMaxAllowedIps = 256;
static constexpr size_t kDashGatewayMaxBlockedIps = 128;
static constexpr size_t kDashGatewayMaxWhitelistEntries = 200;
static constexpr size_t kDashGatewayMaxBlacklistEntries = 100;
static constexpr size_t kDashGatewayMaxCidrEntries = 64;
static constexpr size_t kDashGatewayRuleMaxLen = 96;
static constexpr size_t kDashGatewayDnsCacheEntries = 128;
static constexpr size_t kDashGatewayDnsCacheRespMax = 512;
static constexpr uint32_t kDashGatewayDnsCacheTtlSec = 60;
// Allowed IP TTL: drop entries unseen for this long, so list never overflows
// from CDN IP rotation. 1 hour comfortably covers typical CDN TTLs.
static constexpr uint32_t kDashGatewayAllowedIpTtlSec = 3600;
static constexpr uint32_t kDashGatewayBlockedIpTtlSec = 900;
static constexpr uint32_t kDashGatewayAllowedPruneIntervalSec = 60;
// Pre-resolve whitelist domains in the background so the allowed list is
// already populated before clients connect. One domain per N seconds keeps
// upstream load minimal.
static constexpr uint32_t kDashGatewayWhitelistRefreshIntervalSec = 2;
static constexpr uint32_t kDashGatewayWhitelistFullCycleMinSec = 300; // re-cycle every 5 min minimum

enum DashGatewayDnsMode : uint8_t
{
    DASH_DNS_BLACKLIST = 0,
    DASH_DNS_WHITELIST = 1,
};

struct DashGatewayBlockedDomain
{
    char domain[96];
    uint32_t count;
};

struct DashGatewayPendingQuery
{
    uint16_t originalId;
    uint16_t proxyId;
    uint16_t qtype;
    sockaddr_in clientAddr;
    uint16_t clientIds[kDashGatewayMaxPendingClients];
    sockaddr_in clients[kDashGatewayMaxPendingClients];
    uint8_t clientCount;
    uint32_t rulesVersion;
    TickType_t startTime;
    char domain[256];
    bool inUse;
    bool blackholeLearn;     // true = upstream resolution for IP blackhole
    bool whitelistRefresh;   // true = background prefetch of whitelist IPs
};

struct DashGatewayAllowedIp
{
    uint32_t ip;
    uint32_t lastSeenSec;
};

struct DashGatewayBlockedIp
{
    uint32_t ip;
    uint32_t count;
    uint32_t lastSeenSec;
};

struct DashGatewayDomainRule
{
    char domain[kDashGatewayRuleMaxLen];
    uint8_t len;
};

struct DashGatewayDnsCacheEntry
{
    char domain[128];
    uint16_t qtype;
    uint16_t respLen;
    uint32_t expiresSec;
    uint32_t lastUsedSec;
    uint8_t resp[kDashGatewayDnsCacheRespMax];
};

struct DashGatewayCidrRule
{
    uint32_t netHost;
    uint32_t maskHost;
    uint8_t prefix;
};

static bool gatewayEnabled = true;
static DashGatewayDnsMode gatewayDnsMode = DASH_DNS_BLACKLIST;
static bool gatewayDnsStrict = false;
static String gatewayDnsBlacklist;
static String gatewayDnsWhitelist;
static String gatewayDnsCidrAllowlist;
static uint32_t gatewayUpstreamDns = IPADDR_NONE;
static TaskHandle_t gatewayDnsTaskHandle = nullptr;
static int gatewayDnsSock = -1;
static int gatewayUpstreamSock = -1;
static bool gatewayDnsBindOk = false; // true once DNS socket bound to UDP 53
static uint16_t gatewayNextProxyId = 0;
static bool gatewayNaptEnabled = false;
static DashGatewayBlockedDomain *gatewayBlockedDomains = nullptr;
static bool gatewayBlockedDomainsInPsram = false;
static uint8_t gatewayBlockedDomainCount = 0;
static portMUX_TYPE gatewayBlockedMux = portMUX_INITIALIZER_UNLOCKED;
static DashGatewayPendingQuery *gatewayPendingQueries = nullptr;
static bool gatewayPendingQueriesInPsram = false;
static DashGatewayAllowedIp *gatewayAllowedIps = nullptr;
static bool gatewayAllowedIpsInPsram = false;
static uint16_t gatewayAllowedIpCount = 0;
static portMUX_TYPE gatewayAllowedMux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t gatewayWhitelistRefreshIdx = 0;
static uint32_t gatewayWhitelistRefreshLastSec = 0;
static uint32_t gatewayAllowedPruneLastSec = 0;
static uint32_t gatewayBlockedPruneLastSec = 0;
static DashGatewayBlockedIp *gatewayBlockedIps = nullptr;
static bool gatewayBlockedIpsInPsram = false;
static uint8_t gatewayBlockedIpCount = 0;
static portMUX_TYPE gatewayBlockedIpMux = portMUX_INITIALIZER_UNLOCKED;
static DashGatewayDomainRule *gatewayBlacklistRules = nullptr;
static DashGatewayDomainRule *gatewayWhitelistRules = nullptr;
static bool gatewayBlacklistRulesInPsram = false;
static bool gatewayWhitelistRulesInPsram = false;
static uint16_t gatewayBlacklistRuleCount = 0;
static uint16_t gatewayWhitelistRuleCount = 0;
static DashGatewayDnsCacheEntry *gatewayDnsCache = nullptr;
static bool gatewayDnsCacheInPsram = false;
static uint32_t gatewayDnsCacheHits = 0;
static uint32_t gatewayDnsCacheMisses = 0;
static DashGatewayCidrRule *gatewayCidrRules = nullptr;
static bool gatewayCidrRulesInPsram = false;
static uint16_t gatewayCidrRuleCount = 0;
static uint32_t gatewayDnsRulesVersion = 1;

static const char kDashGatewayDefaultBlacklist[] =
    "tesla.cn\n"
    "tesla.com\n"
    "teslamotors.com\n"
    "tesla.services";

static const char kDashGatewayDefaultWhitelist[] =
    "connman.vn.cloud.tesla.cn\n"
    "nav-prd-maps.tesla.cn\n"
    "maps-cn-prd.go.tesla.services\n"
    "signaling.vn.cloud.tesla.cn\n"
    "api-prd.vn.cloud.tesla.cn\n"
    "media-server-me.tesla.cn";

template <typename T>
static bool dashGatewayAllocateArray(T *&target, size_t count, const char *name, bool &inPsram)
{
    if (target)
        return true;
    inPsram = false;
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void *mem = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem)
    {
        target = static_cast<T *>(mem);
        inPsram = true;
        ESP_LOGI(kDashGatewayTag, "%s cache allocated in PSRAM (%u bytes)",
                 name, static_cast<unsigned>(count * sizeof(T)));
    }
    else
    {
        ESP_LOGW(kDashGatewayTag, "%s PSRAM allocation failed; using internal RAM fallback", name);
    }
#endif
    if (!target)
        target = static_cast<T *>(heap_caps_calloc(count, sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!target)
    {
        ESP_LOGE(kDashGatewayTag, "%s cache allocation failed (%u bytes)",
                 name, static_cast<unsigned>(count * sizeof(T)));
        return false;
    }
    return true;
}

static bool dashGatewayAllocateState()
{
    static bool allocated = false;
    if (allocated)
        return true;
    bool ok = true;
    ok = dashGatewayAllocateArray(gatewayBlockedDomains, 32, "blocked domain", gatewayBlockedDomainsInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayPendingQueries, kDashGatewayMaxPending, "pending DNS query", gatewayPendingQueriesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayAllowedIps, kDashGatewayMaxAllowedIps, "allowed IP", gatewayAllowedIpsInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayBlockedIps, kDashGatewayMaxBlockedIps, "blocked IP", gatewayBlockedIpsInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayBlacklistRules, kDashGatewayMaxBlacklistEntries, "blacklist rule", gatewayBlacklistRulesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayWhitelistRules, kDashGatewayMaxWhitelistEntries, "whitelist rule", gatewayWhitelistRulesInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayDnsCache, kDashGatewayDnsCacheEntries, "DNS response", gatewayDnsCacheInPsram) && ok;
    ok = dashGatewayAllocateArray(gatewayCidrRules, kDashGatewayMaxCidrEntries, "CIDR allowlist", gatewayCidrRulesInPsram) && ok;
    allocated = ok;
    return ok;
}

#define DASH_GATEWAY_FOR_PENDING(q) \
    for (uint8_t q##Idx = 0; q##Idx < kDashGatewayMaxPending; q##Idx++) \
        if (auto &q = gatewayPendingQueries[q##Idx]; true)

static String dashGatewayTrim(String s)
{
    std::string v = static_cast<std::string>(s);
    size_t a = 0;
    while (a < v.size() && std::isspace(static_cast<unsigned char>(v[a])))
        a++;
    size_t b = v.size();
    while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1])))
        b--;
    return v.substr(a, b - a);
}

static String dashGatewayLower(String s)
{
    std::string v = static_cast<std::string>(s);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return v;
}

static String dashGatewayNormalizeDomain(const String &domain)
{
    String d = dashGatewayLower(dashGatewayTrim(domain));
    while (d.endsWith("."))
        d = d.substring(0, d.length() - 1);
    if (d.startsWith("*."))
        d = d.substring(2);
    return d;
}

static uint16_t dashGatewayReadU16(const uint8_t *ptr)
{
    return static_cast<uint16_t>((ptr[0] << 8) | ptr[1]);
}

static void dashGatewayWriteU16(uint8_t *ptr, uint16_t value)
{
    ptr[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    ptr[1] = static_cast<uint8_t>(value & 0xff);
}

static void dashGatewayWriteU32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    ptr[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    ptr[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    ptr[3] = static_cast<uint8_t>(value & 0xff);
}

static bool dashGatewayDomainMatchesRule(const String &domain, const String &rule)
{
    if (domain.length() == 0 || rule.length() == 0)
        return false;
    if (domain == rule)
        return true;
    return domain.length() > rule.length() && domain.endsWith(rule.c_str()) &&
           domain[domain.length() - rule.length() - 1] == '.';
}

static bool dashGatewayDomainMatchesCompiledRule(const char *domain, size_t domainLen, const DashGatewayDomainRule &rule)
{
    if (!domain || domainLen == 0 || rule.len == 0)
        return false;
    if (domainLen == rule.len)
        return std::memcmp(domain, rule.domain, rule.len) == 0;
    return domainLen > rule.len && domain[domainLen - rule.len - 1] == '.' &&
           std::memcmp(domain + domainLen - rule.len, rule.domain, rule.len) == 0;
}

static size_t dashGatewayCompiledRuleMatchLen(const String &domain, const DashGatewayDomainRule *rules, uint16_t count)
{
    String d = dashGatewayNormalizeDomain(domain);
    const char *dc = d.c_str();
    size_t dl = d.length();
    size_t best = 0;
    for (uint16_t i = 0; i < count; i++)
    {
        if (dashGatewayDomainMatchesCompiledRule(dc, dl, rules[i]) && rules[i].len > best)
            best = rules[i].len;
    }
    return best;
}

static void dashGatewayCompileList(const String &list, DashGatewayDomainRule *rules, uint16_t maxRules, uint16_t &outCount)
{
    outCount = 0;
    if (!rules || maxRules == 0)
        return;
    std::memset(rules, 0, sizeof(DashGatewayDomainRule) * maxRules);
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && outCount < maxRules)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        size_t len = std::min(static_cast<size_t>(rule.length()), kDashGatewayRuleMaxLen - 1);
        if (len > 0)
        {
            std::snprintf(rules[outCount].domain, sizeof(rules[outCount].domain), "%.*s",
                          static_cast<int>(len), rule.c_str());
            rules[outCount].len = static_cast<uint8_t>(len);
            outCount++;
        }
        start = end + 1;
    }
}

static void dashGatewayCompileRules()
{
    dashGatewayCompileList(gatewayDnsBlacklist, gatewayBlacklistRules, kDashGatewayMaxBlacklistEntries, gatewayBlacklistRuleCount);
    dashGatewayCompileList(gatewayDnsWhitelist, gatewayWhitelistRules, kDashGatewayMaxWhitelistEntries, gatewayWhitelistRuleCount);
}

static bool dashGatewayParseCidrRule(const String &rule, DashGatewayCidrRule &out)
{
    String item = dashGatewayTrim(rule);
    int slash = item.indexOf('/');
    String ipPart = slash >= 0 ? item.substring(0, slash) : item;
    String prefixPart = slash >= 0 ? item.substring(slash + 1) : "32";
    ipPart = dashGatewayTrim(ipPart);
    prefixPart = dashGatewayTrim(prefixPart);
    if (ipPart.length() == 0 || prefixPart.length() == 0)
        return false;
    char *endPtr = nullptr;
    long prefix = std::strtol(prefixPart.c_str(), &endPtr, 10);
    if (!endPtr || *endPtr != '\0' || prefix < 0 || prefix > 32)
        return false;
    uint32_t ipNbo = inet_addr(ipPart.c_str());
    if (ipNbo == INADDR_NONE || ipNbo == 0)
        return false;
    uint32_t mask = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
    uint32_t ipHost = ntohl(ipNbo);
    out.netHost = ipHost & mask;
    out.maskHost = mask;
    out.prefix = static_cast<uint8_t>(prefix);
    return true;
}

static String dashGatewayFormatCidrRule(const DashGatewayCidrRule &r)
{
    uint32_t netNbo = htonl(r.netHost);
    in_addr a = {};
    a.s_addr = netNbo;
    const char *ip = inet_ntoa(a);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s/%u", ip ? ip : "0.0.0.0", static_cast<unsigned>(r.prefix));
    return String(buf);
}

static String dashGatewaySanitizeCidrAllowlist(const String &list)
{
    String out;
    size_t entryCount = 0;
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && entryCount < kDashGatewayMaxCidrEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        DashGatewayCidrRule parsed = {};
        String raw = String(all.substr(start, end - start).c_str());
        if (dashGatewayParseCidrRule(raw, parsed))
        {
            String normalized = dashGatewayFormatCidrRule(parsed);
            if (out.indexOf(normalized.c_str()) < 0)
            {
                if (out.length() > 0)
                    out += "\n";
                out += normalized;
                entryCount++;
            }
        }
        start = end + 1;
    }
    return out.substring(0, kDashGatewayCidrListMax);
}

static void dashGatewayCompileCidrRules()
{
    gatewayCidrRuleCount = 0;
    if (!gatewayCidrRules)
        return;
    std::memset(gatewayCidrRules, 0, sizeof(DashGatewayCidrRule) * kDashGatewayMaxCidrEntries);
    std::string all = static_cast<std::string>(gatewayDnsCidrAllowlist);
    size_t start = 0;
    while (start <= all.size() && gatewayCidrRuleCount < kDashGatewayMaxCidrEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        DashGatewayCidrRule parsed = {};
        if (dashGatewayParseCidrRule(String(all.substr(start, end - start).c_str()), parsed))
            gatewayCidrRules[gatewayCidrRuleCount++] = parsed;
        start = end + 1;
    }
}

static bool dashGatewayCidrAllows(uint32_t destAddrNbo)
{
    if (!gatewayCidrRules || gatewayCidrRuleCount == 0)
        return false;
    uint32_t ipHost = ntohl(destAddrNbo);
    for (uint16_t i = 0; i < gatewayCidrRuleCount; i++)
    {
        const DashGatewayCidrRule &r = gatewayCidrRules[i];
        if ((ipHost & r.maskHost) == r.netHost)
            return true;
    }
    return false;
}

static void dashGatewayCompileAllRules()
{
    dashGatewayCompileRules();
    dashGatewayCompileCidrRules();
}

static void dashGatewayTrackBlocked(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return;
    portENTER_CRITICAL(&gatewayBlockedMux);
    for (uint8_t i = 0; i < gatewayBlockedDomainCount; i++)
    {
        if (d == gatewayBlockedDomains[i].domain)
        {
            gatewayBlockedDomains[i].count++;
            portEXIT_CRITICAL(&gatewayBlockedMux);
            return;
        }
    }
    uint8_t slot = gatewayBlockedDomainCount < 32 ? gatewayBlockedDomainCount++ : 31;
    std::snprintf(gatewayBlockedDomains[slot].domain, sizeof(gatewayBlockedDomains[slot].domain), "%s", d.c_str());
    gatewayBlockedDomains[slot].count = 1;
    portEXIT_CRITICAL(&gatewayBlockedMux);
}

static size_t dashGatewayDomainRuleMatchLen(const String &domain, const String &list)
{
    String d = dashGatewayNormalizeDomain(domain);
    std::string all = static_cast<std::string>(list);
    size_t best = 0;
    size_t start = 0;
    while (start <= all.size())
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        std::string item = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
        String rule = dashGatewayNormalizeDomain(item);
        if (dashGatewayDomainMatchesRule(d, rule) && rule.length() > best)
            best = rule.length();
        start = end + 1;
    }
    return best;
}

static bool dashGatewayDomainInList(const String &domain, const String &list)
{
    return dashGatewayDomainRuleMatchLen(domain, list) > 0;
}

static bool dashGatewayDomainAllowedForWhitelist(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return false;
    // Allow specific child domains as exceptions under a blocked root domain,
    // but reject a whitelist rule that exactly re-opens the blocked root.
    size_t blockLen = dashGatewayDomainRuleMatchLen(d, gatewayDnsBlacklist);
    return blockLen == 0 || blockLen < d.length();
}

static size_t dashGatewayCountEntries(const String &list)
{
    std::string all = static_cast<std::string>(list);
    size_t count = 0;
    size_t start = 0;
    while (start < all.size())
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        if (end > start)
            count++;
        start = end + 1;
    }
    return count;
}

static bool dashGatewayAppendWhitelist(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (!dashGatewayDomainAllowedForWhitelist(d) || dashGatewayDomainInList(d, gatewayDnsWhitelist))
        return false;
    if (dashGatewayCountEntries(gatewayDnsWhitelist) >= kDashGatewayMaxWhitelistEntries)
        return false;
    String next = gatewayDnsWhitelist;
    if (next.length() > 0 && !next.endsWith("\n"))
        next += "\n";
    next += d;
    if (next.length() > kDashGatewayListMax)
        return false;
    gatewayDnsWhitelist = next;
    return true;
}

static String dashGatewaySanitizeWhitelist(const String &list)
{
    String out;
    size_t entryCount = 0;
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && entryCount < kDashGatewayMaxWhitelistEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        if (rule.length() > 0 && dashGatewayDomainAllowedForWhitelist(rule) && !dashGatewayDomainInList(rule, out))
        {
            if (out.length() > 0)
                out += "\n";
            out += rule;
            entryCount++;
        }
        start = end + 1;
    }
    return out.substring(0, kDashGatewayListMax);
}

static String dashGatewaySanitizeBlacklist(const String &list)
{
    String out;
    size_t entryCount = 0;
    std::string all = static_cast<std::string>(list);
    size_t start = 0;
    while (start <= all.size() && entryCount < kDashGatewayMaxBlacklistEntries)
    {
        while (start < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[start]);
            if (!std::isspace(c) && all[start] != ',' && all[start] != ';')
                break;
            start++;
        }
        if (start >= all.size())
            break;
        size_t end = start;
        while (end < all.size())
        {
            unsigned char c = static_cast<unsigned char>(all[end]);
            if (std::isspace(c) || all[end] == ',' || all[end] == ';')
                break;
            end++;
        }
        String rule = dashGatewayNormalizeDomain(all.substr(start, end - start));
        if (rule.length() > 0 && !dashGatewayDomainInList(rule, out))
        {
            if (out.length() > 0)
                out += "\n";
            out += rule;
            entryCount++;
        }
        start = end + 1;
    }
    return out.substring(0, kDashGatewayListMax);
}

static bool dashGatewayHardcodedWhitelist(const String &domain)
{
    // *.cdnhwcaoc115.cn (Alibaba Cloud CDN)
    if (dashGatewayDomainMatchesRule(domain, "cdnhwcaoc115.cn"))
        return true;
    // sdk.51.la
    if (domain == "sdk.51.la")
        return true;
    return false;
}

static bool dashGatewayDnsAllowed(const String &domain)
{
    String d = dashGatewayNormalizeDomain(domain);
    if (d.length() == 0)
        return false;
    // Hardcoded whitelist always passes
    if (dashGatewayHardcodedWhitelist(d))
        return true;
    size_t blockLen = dashGatewayCompiledRuleMatchLen(d, gatewayBlacklistRules, gatewayBlacklistRuleCount);
    size_t allowLen = dashGatewayCompiledRuleMatchLen(d, gatewayWhitelistRules, gatewayWhitelistRuleCount);
    if (allowLen > 0 && allowLen >= blockLen)
        return true;
    if (blockLen > 0)
        return false;
    if (gatewayDnsMode == DASH_DNS_WHITELIST)
        return allowLen > 0;
    return true;
}

static String dashGatewayDnsDecisionJson(const String &input)
{
    String domain = dashGatewayNormalizeDomain(input);
    size_t blockLen = dashGatewayCompiledRuleMatchLen(domain, gatewayBlacklistRules, gatewayBlacklistRuleCount);
    size_t allowLen = dashGatewayCompiledRuleMatchLen(domain, gatewayWhitelistRules, gatewayWhitelistRuleCount);
    bool blacklisted = blockLen > 0;
    bool whitelisted = allowLen > 0;
    bool allowed = domain.length() > 0 && dashGatewayDnsAllowed(domain);
    String j = "{\"domain\":\"";
    j += jsonEscape(domain.c_str());
    j += "\",\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"mode\":";
    j += String(static_cast<int>(gatewayDnsMode));
    j += ",\"allowed\":";
    j += allowed ? "true" : "false";
    j += ",\"blocked\":";
    j += (!allowed && gatewayEnabled && domain.length() > 0) ? "true" : "false";
    j += ",\"blacklisted\":";
    j += blacklisted ? "true" : "false";
    j += ",\"whitelisted\":";
    j += whitelisted ? "true" : "false";
    j += ",\"reason\":\"";
    if (!gatewayEnabled)
        j += "gateway disabled";
    else if (domain.length() == 0)
        j += "empty domain";
    else if (whitelisted && blacklisted && allowLen >= blockLen)
        j += "whitelist override blacklist";
    else if (gatewayDnsMode == DASH_DNS_BLACKLIST)
        j += blacklisted ? "matched blacklist" : "not in blacklist";
    else
        j += whitelisted ? "matched whitelist" : "not in whitelist";
    j += "\"}";
    return j;
}

__attribute__((unused)) static bool dashGatewayParseDnsName(const uint8_t *buf, size_t len, String &out)
{
    if (len < 13)
        return false;
    size_t pos = 12;
    std::string name;
    while (pos < len)
    {
        uint8_t n = buf[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > len)
            return false;
        if (!name.empty())
            name += '.';
        name.append(reinterpret_cast<const char *>(buf + pos), n);
        pos += n;
    }
    if (name.empty())
        return false;
    out = name;
    return true;
}

static bool dashGatewayParseDnsQuestion(const uint8_t *buf, size_t len, String &name, uint16_t &qtype, size_t *questionEnd = nullptr)
{
    if (len < 16)
        return false;
    size_t pos = 12;
    std::string out;
    while (pos < len)
    {
        uint8_t n = buf[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > len)
            return false;
        if (!out.empty())
            out += '.';
        out.append(reinterpret_cast<const char *>(buf + pos), n);
        pos += n;
    }
    if (out.empty() || pos + 4 > len)
        return false;
    qtype = dashGatewayReadU16(buf + pos);
    if (questionEnd)
        *questionEnd = pos + 4;
    name = out;
    return true;
}

static void dashGatewayDnsCacheClear()
{
    if (!gatewayDnsCache)
        return;
    std::memset(gatewayDnsCache, 0, sizeof(DashGatewayDnsCacheEntry) * kDashGatewayDnsCacheEntries);
}

static size_t dashGatewayDnsCacheLookup(const String &domain, uint16_t qtype, uint32_t nowSec, uint8_t *out, size_t outCap)
{
    if (!gatewayDnsCache || domain.length() == 0)
        return 0;
    String d = dashGatewayNormalizeDomain(domain);
    for (size_t i = 0; i < kDashGatewayDnsCacheEntries; i++)
    {
        DashGatewayDnsCacheEntry &e = gatewayDnsCache[i];
        if (e.expiresSec == 0)
            continue;
        if (static_cast<int32_t>(e.expiresSec - nowSec) <= 0)
        {
            e.expiresSec = 0;
            continue;
        }
        if (e.qtype != qtype || e.respLen == 0 || e.respLen > outCap)
            continue;
        if (std::strcmp(e.domain, d.c_str()) != 0)
            continue;
        std::memcpy(out, e.resp, e.respLen);
        e.lastUsedSec = nowSec;
        gatewayDnsCacheHits++;
        return e.respLen;
    }
    gatewayDnsCacheMisses++;
    return 0;
}

static void dashGatewayDnsCachePut(const String &domain, uint16_t qtype, uint32_t nowSec, const uint8_t *resp, size_t respLen)
{
    if (!gatewayDnsCache || domain.length() == 0 || !resp || respLen < 12 || respLen > kDashGatewayDnsCacheRespMax)
        return;
    // Cache successful answers only. NXDOMAIN/REFUSED and empty answers should
    // reflect live policy/upstream state rather than linger in the bridge.
    if ((resp[3] & 0x0F) != 0 || dashGatewayReadU16(resp + 6) == 0)
        return;
    String d = dashGatewayNormalizeDomain(domain);
    size_t target = kDashGatewayDnsCacheEntries;
    uint32_t oldest = 0xFFFFFFFFu;
    for (size_t i = 0; i < kDashGatewayDnsCacheEntries; i++)
    {
        DashGatewayDnsCacheEntry &e = gatewayDnsCache[i];
        if (e.expiresSec == 0)
        {
            target = i;
            break;
        }
        if (e.qtype == qtype && std::strcmp(e.domain, d.c_str()) == 0)
        {
            target = i;
            break;
        }
        if (e.lastUsedSec < oldest)
        {
            oldest = e.lastUsedSec;
            target = i;
        }
    }
    if (target >= kDashGatewayDnsCacheEntries)
        return;
    DashGatewayDnsCacheEntry &e = gatewayDnsCache[target];
    std::memset(&e, 0, sizeof(e));
    std::snprintf(e.domain, sizeof(e.domain), "%s", d.c_str());
    e.qtype = qtype;
    e.respLen = static_cast<uint16_t>(respLen);
    e.expiresSec = nowSec + kDashGatewayDnsCacheTtlSec;
    e.lastUsedSec = nowSec;
    std::memcpy(e.resp, resp, respLen);
}

static bool dashGatewayPendingAddClient(DashGatewayPendingQuery &q, uint16_t origId, const sockaddr_in &client)
{
    if (q.clientCount >= kDashGatewayMaxPendingClients)
        return false;
    q.clientIds[q.clientCount] = origId;
    q.clients[q.clientCount] = client;
    q.clientCount++;
    return true;
}

static bool dashGatewayAttachDuplicatePending(const String &domain, uint16_t qtype, uint16_t origId, const sockaddr_in &client)
{
    String d = dashGatewayNormalizeDomain(domain);
    DASH_GATEWAY_FOR_PENDING(q)
    {
        if (!q.inUse || q.blackholeLearn || q.whitelistRefresh || q.rulesVersion != gatewayDnsRulesVersion)
            continue;
        if (q.qtype == qtype && std::strcmp(q.domain, d.c_str()) == 0)
            return dashGatewayPendingAddClient(q, origId, client);
    }
    return false;
}

static void dashGatewayInitPending(DashGatewayPendingQuery &q, uint16_t origId, uint16_t proxyId, uint16_t qtype,
                                   const sockaddr_in *client, const String &domain,
                                   bool blackholeLearn, bool whitelistRefresh)
{
    std::memset(&q, 0, sizeof(q));
    q.originalId = origId;
    q.proxyId = proxyId;
    q.qtype = qtype;
    q.startTime = xTaskGetTickCount();
    q.inUse = true;
    q.blackholeLearn = blackholeLearn;
    q.whitelistRefresh = whitelistRefresh;
    q.rulesVersion = gatewayDnsRulesVersion;
    String d = dashGatewayNormalizeDomain(domain);
    std::snprintf(q.domain, sizeof(q.domain), "%s", d.c_str());
    if (client)
    {
        q.clientAddr = *client;
        dashGatewayPendingAddClient(q, origId, *client);
    }
}

static size_t dashGatewayMakeDnsBlockedReply(const uint8_t *query, size_t qlen, uint8_t *reply, size_t cap)
{
    if (qlen < 12)
        return 0;

    size_t pos = 12;
    while (pos < qlen)
    {
        uint8_t n = query[pos++];
        if (n == 0)
            break;
        if ((n & 0xC0) != 0 || n > 63 || pos + n > qlen)
            return 0;
        pos += n;
    }
    if (pos + 4 > qlen)
        return 0;

    const size_t questionEnd = pos + 4;
    const uint16_t qType = dashGatewayReadU16(query + pos);
    const uint16_t flags = dashGatewayReadU16(query + 2);
    const uint16_t responseFlags = static_cast<uint16_t>(0x8000 | 0x0080 | (flags & 0x0100));
    const bool answerA = qType == kDashGatewayDnsTypeA;
    const size_t answerLen = answerA ? 16 : 0;
    const size_t outLen = questionEnd + answerLen;
    if (outLen > cap)
        return 0;

    std::memcpy(reply, query, questionEnd);
    dashGatewayWriteU16(reply + 2, responseFlags);
    dashGatewayWriteU16(reply + 4, 1);
    dashGatewayWriteU16(reply + 6, answerA ? 1 : 0);
    dashGatewayWriteU16(reply + 8, 0);
    dashGatewayWriteU16(reply + 10, 0);

    if (!answerA)
        return outLen;

    size_t offset = questionEnd;
    reply[offset++] = 0xC0;
    reply[offset++] = 0x0C;
    dashGatewayWriteU16(reply + offset, kDashGatewayDnsTypeA);
    offset += 2;
    dashGatewayWriteU16(reply + offset, kDashGatewayDnsClassIN);
    offset += 2;
    dashGatewayWriteU32(reply + offset, kDashGatewayBlockedTtlSeconds);
    offset += 4;
    dashGatewayWriteU16(reply + offset, 4);
    offset += 2;
    reply[offset++] = 0;
    reply[offset++] = 0;
    reply[offset++] = 0;
    reply[offset++] = 0;
    return outLen;
}

static size_t dashGatewayMakeDnsFakeReply(const uint8_t *query, size_t qlen, uint8_t *reply, size_t cap, uint32_t fakeIp)
{
    if (qlen < 12 || qlen + 16 > cap)
        return 0;
    std::memcpy(reply, query, qlen);
    reply[2] = 0x81; reply[3] = 0x80;
    reply[6] = 0x00; reply[7] = 0x01;
    reply[8] = reply[9] = reply[10] = reply[11] = 0;
    reply[qlen]   = 0xC0; reply[qlen+1] = 0x0C;
    reply[qlen+2] = 0x00; reply[qlen+3] = 0x01;
    reply[qlen+4] = 0x00; reply[qlen+5] = 0x01;
    reply[qlen+6] = reply[qlen+7] = reply[qlen+8] = reply[qlen+9] = 0;
    reply[qlen+10] = 0x00; reply[qlen+11] = 0x04;
    const auto *ip = reinterpret_cast<const uint8_t *>(&fakeIp);
    reply[qlen+12] = ip[0]; reply[qlen+13] = ip[1];
    reply[qlen+14] = ip[2]; reply[qlen+15] = ip[3];
    return qlen + 16;
}

static uint32_t dashGatewayIpHash(uint32_t ip, uint32_t tableSize)
{
    uint32_t h = ip ^ (ip >> 16);
    h *= 2654435761u;
    return tableSize ? (h % tableSize) : 0;
}

static void dashGatewayTrackAllowedIp(uint32_t ip)
{
    if (ip == 0 || ip == 0xFFFFFFFFu) return;
    uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    portENTER_CRITICAL(&gatewayAllowedMux);
    uint16_t oldest = 0;
    bool haveOldest = false;
    uint32_t start = dashGatewayIpHash(ip, kDashGatewayMaxAllowedIps);
    for (uint16_t probe = 0; probe < kDashGatewayMaxAllowedIps; probe++)
    {
        uint16_t i = static_cast<uint16_t>((start + probe) % kDashGatewayMaxAllowedIps);
        if (gatewayAllowedIps[i].ip == ip)
        {
            gatewayAllowedIps[i].lastSeenSec = nowSec;
            portEXIT_CRITICAL(&gatewayAllowedMux);
            return;
        }
        if (gatewayAllowedIps[i].ip == 0)
        {
            gatewayAllowedIps[i].ip = ip;
            gatewayAllowedIps[i].lastSeenSec = nowSec;
            if (gatewayAllowedIpCount < kDashGatewayMaxAllowedIps)
                gatewayAllowedIpCount++;
            portEXIT_CRITICAL(&gatewayAllowedMux);
            return;
        }
        if (!haveOldest || gatewayAllowedIps[i].lastSeenSec < gatewayAllowedIps[oldest].lastSeenSec)
        {
            oldest = i;
            haveOldest = true;
        }
    }
    gatewayAllowedIps[oldest].ip = ip;
    gatewayAllowedIps[oldest].lastSeenSec = nowSec;
    portEXIT_CRITICAL(&gatewayAllowedMux);
}

static bool dashGatewayAllowedIpContains(uint32_t ip)
{
    if (ip == 0 || ip == 0xFFFFFFFFu || gatewayAllowedIpCount == 0)
        return false;
    bool found = false;
    portENTER_CRITICAL(&gatewayAllowedMux);
    uint32_t start = dashGatewayIpHash(ip, kDashGatewayMaxAllowedIps);
    for (uint16_t probe = 0; probe < kDashGatewayMaxAllowedIps; probe++)
    {
        uint16_t i = static_cast<uint16_t>((start + probe) % kDashGatewayMaxAllowedIps);
        if (gatewayAllowedIps[i].ip == ip) { found = true; break; }
        if (gatewayAllowedIps[i].ip == 0) break;
    }
    portEXIT_CRITICAL(&gatewayAllowedMux);
    return found;
}

// Drop entries unseen for kDashGatewayAllowedIpTtlSec. Cheap O(N) scan with
// in-place compaction; called at most every kDashGatewayAllowedPruneIntervalSec.
static void dashGatewayPruneAllowedIps(uint32_t nowSec)
{
    DashGatewayAllowedIp snapshot[kDashGatewayMaxAllowedIps];
    uint16_t keep = 0;
    portENTER_CRITICAL(&gatewayAllowedMux);
    for (uint16_t i = 0; i < kDashGatewayMaxAllowedIps; i++)
    {
        if (gatewayAllowedIps[i].ip != 0 && nowSec - gatewayAllowedIps[i].lastSeenSec <= kDashGatewayAllowedIpTtlSec)
            snapshot[keep++] = gatewayAllowedIps[i];
    }
    std::memset(gatewayAllowedIps, 0, sizeof(DashGatewayAllowedIp) * kDashGatewayMaxAllowedIps);
    gatewayAllowedIpCount = 0;
    for (uint16_t s = 0; s < keep; s++)
    {
        uint32_t start = dashGatewayIpHash(snapshot[s].ip, kDashGatewayMaxAllowedIps);
        for (uint16_t probe = 0; probe < kDashGatewayMaxAllowedIps; probe++)
        {
            uint16_t i = static_cast<uint16_t>((start + probe) % kDashGatewayMaxAllowedIps);
            if (gatewayAllowedIps[i].ip == 0)
            {
                gatewayAllowedIps[i] = snapshot[s];
                gatewayAllowedIpCount++;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&gatewayAllowedMux);
}

// Returns Nth domain rule from a newline-separated list, normalized lowercase
// and trimmed. Returns empty string when idx is past the end.
static String dashGatewayGetRuleByIndex(const String &list, uint16_t idx)
{
    const std::string &s = static_cast<std::string>(list);
    size_t cur = 0, count = 0;
    while (cur < s.size())
    {
        while (cur < s.size() && (s[cur] == '\n' || s[cur] == '\r' || s[cur] == ' ' || s[cur] == ',' || s[cur] == ';'))
            cur++;
        if (cur >= s.size()) break;
        size_t start = cur;
        while (cur < s.size() && s[cur] != '\n' && s[cur] != '\r' && s[cur] != ',' && s[cur] != ';')
            cur++;
        String rule = String(s.substr(start, cur - start).c_str());
        rule = dashGatewayNormalizeDomain(rule);
        if (rule.length() > 0)
        {
            if (count == idx) return rule;
            count++;
        }
    }
    return String("");
}

// Build a DNS A-record query for `domain` with the given transaction id into buf.
// Returns the encoded length, or 0 on overflow / invalid label.
static size_t dashGatewayBuildDnsQuery(const String &domain, uint16_t txid, uint8_t *buf, size_t cap)
{
    if (cap < 12 + 5 + domain.length()) return 0;
    buf[0] = txid >> 8; buf[1] = txid & 0xFF;
    buf[2] = 0x01; buf[3] = 0x00; // standard query, RD=1
    buf[4] = 0; buf[5] = 1;       // QDCOUNT=1
    buf[6] = 0; buf[7] = 0;       // ANCOUNT=0
    buf[8] = 0; buf[9] = 0;
    buf[10] = 0; buf[11] = 0;
    size_t pos = 12;
    const char *d = domain.c_str();
    size_t labelStart = 0;
    size_t i = 0;
    for (; d[i] != '\0'; i++)
    {
        if (d[i] == '.')
        {
            size_t labelLen = i - labelStart;
            if (labelLen == 0 || labelLen > 63 || pos + 1 + labelLen + 5 > cap) return 0;
            buf[pos++] = static_cast<uint8_t>(labelLen);
            std::memcpy(buf + pos, d + labelStart, labelLen);
            pos += labelLen;
            labelStart = i + 1;
        }
    }
    size_t labelLen = i - labelStart;
    if (labelLen > 63 || pos + 1 + labelLen + 5 > cap) return 0;
    if (labelLen > 0)
    {
        buf[pos++] = static_cast<uint8_t>(labelLen);
        std::memcpy(buf + pos, d + labelStart, labelLen);
        pos += labelLen;
    }
    buf[pos++] = 0;            // root
    buf[pos++] = 0; buf[pos++] = 1;  // QTYPE=A
    buf[pos++] = 0; buf[pos++] = 1;  // QCLASS=IN
    return pos;
}

__attribute__((unused)) static void dashGatewayTrackBlockedIp(uint32_t ip)
{
    if (ip == 0 || ip == 0xFFFFFFFFu) return;
    uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    uint8_t oldest = 0;
    bool haveOldest = false;
    uint32_t start = dashGatewayIpHash(ip, kDashGatewayMaxBlockedIps);
    for (uint8_t probe = 0; probe < kDashGatewayMaxBlockedIps; probe++)
    {
        uint8_t i = static_cast<uint8_t>((start + probe) % kDashGatewayMaxBlockedIps);
        if (gatewayBlockedIps[i].ip == ip)
        {
            gatewayBlockedIps[i].count++;
            gatewayBlockedIps[i].lastSeenSec = nowSec;
            portEXIT_CRITICAL(&gatewayBlockedIpMux);
            return;
        }
        if (gatewayBlockedIps[i].ip == 0)
        {
            gatewayBlockedIps[i].ip = ip;
            gatewayBlockedIps[i].count = 1;
            gatewayBlockedIps[i].lastSeenSec = nowSec;
            if (gatewayBlockedIpCount < kDashGatewayMaxBlockedIps)
                gatewayBlockedIpCount++;
            portEXIT_CRITICAL(&gatewayBlockedIpMux);
            return;
        }
        if (!haveOldest || gatewayBlockedIps[i].lastSeenSec < gatewayBlockedIps[oldest].lastSeenSec)
        {
            oldest = i;
            haveOldest = true;
        }
    }
    gatewayBlockedIps[oldest].ip = ip;
    gatewayBlockedIps[oldest].count = 1;
    gatewayBlockedIps[oldest].lastSeenSec = nowSec;
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
}

// Self-learning IP blackhole: add IP to blockedIps with count=0 so the hook
// reports first-real-drop as count=1. Dedup against existing entries.
static void dashGatewayLearnBlackholeIp(uint32_t ip)
{
    if (ip == 0 || ip == 0xFFFFFFFFu) return;
    uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    uint8_t oldest = 0;
    bool haveOldest = false;
    uint32_t start = dashGatewayIpHash(ip, kDashGatewayMaxBlockedIps);
    for (uint8_t probe = 0; probe < kDashGatewayMaxBlockedIps; probe++)
    {
        uint8_t i = static_cast<uint8_t>((start + probe) % kDashGatewayMaxBlockedIps);
        if (gatewayBlockedIps[i].ip == ip)
        {
            gatewayBlockedIps[i].lastSeenSec = nowSec;
            portEXIT_CRITICAL(&gatewayBlockedIpMux);
            return;
        }
        if (gatewayBlockedIps[i].ip == 0)
        {
            gatewayBlockedIps[i].ip = ip;
            gatewayBlockedIps[i].count = 0;
            gatewayBlockedIps[i].lastSeenSec = nowSec;
            if (gatewayBlockedIpCount < kDashGatewayMaxBlockedIps)
                gatewayBlockedIpCount++;
            portEXIT_CRITICAL(&gatewayBlockedIpMux);
            return;
        }
        if (!haveOldest || gatewayBlockedIps[i].lastSeenSec < gatewayBlockedIps[oldest].lastSeenSec)
        {
            oldest = i;
            haveOldest = true;
        }
    }
    gatewayBlockedIps[oldest].ip = ip;
    gatewayBlockedIps[oldest].count = 0;
    gatewayBlockedIps[oldest].lastSeenSec = nowSec;
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
}

static bool dashGatewayBlockedIpContainsAndBump(uint32_t ip)
{
    if (ip == 0 || ip == 0xFFFFFFFFu || gatewayBlockedIpCount == 0)
        return false;
    uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    bool found = false;
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    uint32_t start = dashGatewayIpHash(ip, kDashGatewayMaxBlockedIps);
    for (uint8_t probe = 0; probe < kDashGatewayMaxBlockedIps; probe++)
    {
        uint8_t i = static_cast<uint8_t>((start + probe) % kDashGatewayMaxBlockedIps);
        if (gatewayBlockedIps[i].ip == ip)
        {
            gatewayBlockedIps[i].count++;
            gatewayBlockedIps[i].lastSeenSec = nowSec;
            found = true;
            break;
        }
        if (gatewayBlockedIps[i].ip == 0) break;
    }
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
    return found;
}

static void dashGatewayPruneBlockedIps(uint32_t nowSec)
{
    DashGatewayBlockedIp snapshot[kDashGatewayMaxBlockedIps];
    uint8_t keep = 0;
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    for (uint8_t i = 0; i < kDashGatewayMaxBlockedIps; i++)
    {
        if (gatewayBlockedIps[i].ip != 0 && nowSec - gatewayBlockedIps[i].lastSeenSec <= kDashGatewayBlockedIpTtlSec)
            snapshot[keep++] = gatewayBlockedIps[i];
    }
    std::memset(gatewayBlockedIps, 0, sizeof(DashGatewayBlockedIp) * kDashGatewayMaxBlockedIps);
    gatewayBlockedIpCount = 0;
    for (uint8_t s = 0; s < keep; s++)
    {
        uint32_t start = dashGatewayIpHash(snapshot[s].ip, kDashGatewayMaxBlockedIps);
        for (uint8_t probe = 0; probe < kDashGatewayMaxBlockedIps; probe++)
        {
            uint8_t i = static_cast<uint8_t>((start + probe) % kDashGatewayMaxBlockedIps);
            if (gatewayBlockedIps[i].ip == 0)
            {
                gatewayBlockedIps[i] = snapshot[s];
                gatewayBlockedIpCount++;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
}

// Walk DNS response and add every A-record IP to the blackhole cache.
// Same parse logic as dashGatewayExtractAllowedIps but inserts via blackhole.
static void dashGatewayExtractBlackholeIps(const uint8_t *buf, int len)
{
    if (len <= 12) return;
    size_t pos = 12;
    bool ptrSeen = false;
    while (pos < static_cast<size_t>(len) && buf[pos] != 0 && !ptrSeen)
    {
        if ((buf[pos] & 0xC0) == 0xC0) { pos += 2; ptrSeen = true; break; }
        pos += buf[pos] + 1;
    }
    if (!ptrSeen && pos < static_cast<size_t>(len)) pos++;
    pos += 4;
    uint16_t ancount = static_cast<uint16_t>((buf[6] << 8) | buf[7]);
    for (uint16_t ai = 0; ai < ancount && pos + 10 <= static_cast<size_t>(len); ai++)
    {
        if ((buf[pos] & 0xC0) == 0xC0) pos += 2;
        else
        {
            while (pos < static_cast<size_t>(len) && buf[pos] != 0)
                pos += buf[pos] + 1;
            if (pos >= static_cast<size_t>(len)) break;
            pos++;
        }
        if (pos + 10 > static_cast<size_t>(len)) break;
        uint16_t type  = static_cast<uint16_t>((buf[pos] << 8) | buf[pos+1]);
        uint16_t rdlen = static_cast<uint16_t>((buf[pos+8] << 8) | buf[pos+9]);
        if (type == 1 && rdlen == 4 && pos + 10 + 4 <= static_cast<size_t>(len))
        {
            uint32_t ip;
            std::memcpy(&ip, buf + pos + 10, 4);
            dashGatewayLearnBlackholeIp(ip);
        }
        pos += 10 + rdlen;
    }
}

static void dashGatewayExtractAllowedIps(const uint8_t *buf, int len)
{
    if (len <= 12)
        return;
    size_t pos = 12;
    bool ptrSeen = false;
    while (pos < static_cast<size_t>(len) && buf[pos] != 0 && !ptrSeen)
    {
        if ((buf[pos] & 0xC0) == 0xC0) { pos += 2; ptrSeen = true; break; }
        pos += buf[pos] + 1;
    }
    if (!ptrSeen && pos < static_cast<size_t>(len))
        pos++;
    pos += 4; // skip QTYPE + QCLASS
    uint16_t ancount = static_cast<uint16_t>((buf[6] << 8) | buf[7]);
    for (uint16_t ai = 0; ai < ancount && pos + 10 <= static_cast<size_t>(len); ai++)
    {
        if ((buf[pos] & 0xC0) == 0xC0)
            pos += 2;
        else
        {
            while (pos < static_cast<size_t>(len) && buf[pos] != 0)
                pos += buf[pos] + 1;
            if (pos >= static_cast<size_t>(len)) break;
            pos++;
        }
        if (pos + 10 > static_cast<size_t>(len)) break;
        uint16_t type  = static_cast<uint16_t>((buf[pos] << 8) | buf[pos+1]);
        uint16_t rdlen = static_cast<uint16_t>((buf[pos+8] << 8) | buf[pos+9]);
        if (type == 1 && rdlen == 4 && pos + 10 + 4 <= static_cast<size_t>(len))
        {
            uint32_t ip;
            std::memcpy(&ip, buf + pos + 10, 4);
            dashGatewayTrackAllowedIp(ip);
        }
        pos += 10 + rdlen;
    }
}

__attribute__((unused)) static bool dashGatewayIsIpAllowed(uint32_t ip)
{
    if (!gatewayDnsStrict)
        return true;
    return dashGatewayAllowedIpContains(ip);
}

// 鈹€鈹€ lwIP IP4_CANFORWARD hook 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Wired in via include/lwip_hooks.h + -DESP_IDF_LWIP_HOOK_FILENAME in
// platformio.ini. Called per outbound IPv4 packet during NAPT forwarding
// (AP鈫扴TA direction). Hot path 鈥?short-circuits as cheaply as possible when
// strict mode is off, which is the default.
//
// Returns nonzero (forward) or 0 (drop).
//
// Design constraints:
//   * Whitelist + strict: drops dest IPs that were never learned via our
//     captive DNS 鈥?catches clients that bypass DNS by hardcoding an IP or
//     using 8.8.8.8. gatewayAllowedIps is populated from upstream A-record
//     responses by dashGatewayExtractAllowedIps().
//   * Blacklist + strict: today only drops IPs that have already been added
//     to gatewayBlockedIps (currently nothing populates that set during
//     normal traffic 鈥?a periodic resolver would be needed to harvest IPs
//     from blacklist domains. Tracked as a follow-up; see report).
//   * Always-allow ranges keep AP-internal traffic, multicast, link-local,
//     loopback, broadcast and the upstream DNS server reachable regardless
//     of mode, so we don't break DHCP/DNS or accidentally cut off the
//     captive web UI on 100.100.1.1.
extern "C" int dashGatewayHookIp4CanForward(unsigned int destAddrNbo)
{
    if (!gatewayEnabled)
        return 1; // gateway off 鈥?default forward

    // Decode bytes from network-order address (lwIP stores NBO on little-endian).
    const uint8_t a = static_cast<uint8_t>(destAddrNbo & 0xFFu);
    const uint8_t b = static_cast<uint8_t>((destAddrNbo >> 8) & 0xFFu);

    // Always-allow ranges (apply to every check below):
    if (a == 100 && b == 100) return 1;          // AP subnet
    if (a >= 224 && a <= 239) return 1;          // multicast
    if (a == 169 && b == 254) return 1;          // link-local
    if (a == 127) return 1;                      // loopback
    if (a == 0) return 1;                        // DHCP discover
    if (destAddrNbo == 0xFFFFFFFFu) return 1;    // limited broadcast
    if (gatewayUpstreamDns != IPADDR_NONE && destAddrNbo == gatewayUpstreamDns)
        return 1;                                // upstream DNS reachable

    // Explicit CIDR allowlist bypasses strict/blackhole checks for services
    // that legitimately connect by IP or CDN range without a stable hostname.
    if (dashGatewayCidrAllows(destAddrNbo))
        return 1;

    // Always: blackhole cache (auto-learned IPs from blocked DNS queries +
    // strict-mode drop entries). Catches clients hardcoding IPs to bypass DNS.
    if (dashGatewayBlockedIpContainsAndBump(destAddrNbo))
        return 0;

    // Strict mode adds whitelist-only enforcement on top of the blackhole.
    if (!gatewayDnsStrict) return 1;

    if (gatewayDnsMode == DASH_DNS_WHITELIST)
    {
        bool ok = dashGatewayAllowedIpContains(destAddrNbo);
        if (!ok)
            dashGatewayTrackBlockedIp(destAddrNbo);
        return ok ? 1 : 0;
    }
    // Strict blacklist mode: blackhole already handled above; default forward.
    return 1;
}

static void dashGatewayDnsTask(void *)
{
    uint8_t rx[512];
    uint8_t tx[512];

    gatewayUpstreamSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gatewayUpstreamSock < 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS upstream socket failed");
        vTaskDelete(nullptr);
        return;
    }
    int flags = fcntl(gatewayUpstreamSock, F_GETFL, 0);
    fcntl(gatewayUpstreamSock, F_SETFL, flags | O_NONBLOCK);

    DASH_GATEWAY_FOR_PENDING(q)
        q.inUse = false;

    TickType_t lastCleanup = xTaskGetTickCount();

    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(gatewayDnsSock, &rfds);
        FD_SET(gatewayUpstreamSock, &rfds);
        int maxFd = gatewayDnsSock > gatewayUpstreamSock ? gatewayDnsSock : gatewayUpstreamSock;
        timeval tv = {}; tv.tv_sec = 1;
        int ret = select(maxFd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0)
            continue;

        // Periodic cleanup of timed-out pending queries
        if (xTaskGetTickCount() - lastCleanup > pdMS_TO_TICKS(1000))
        {
            TickType_t now = xTaskGetTickCount();
            DASH_GATEWAY_FOR_PENDING(q)
                if (q.inUse && (int32_t)(now - q.startTime) > (int32_t)pdMS_TO_TICKS(5000))
                    q.inUse = false;
            lastCleanup = xTaskGetTickCount();

            uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
            // Prune stale allowed IPs.
            if (nowSec - gatewayAllowedPruneLastSec >= kDashGatewayAllowedPruneIntervalSec)
            {
                dashGatewayPruneAllowedIps(nowSec);
                dashGatewayPruneBlockedIps(nowSec);
                gatewayAllowedPruneLastSec = nowSec;
                gatewayBlockedPruneLastSec = nowSec;
            }
            // Background whitelist prefetch 鈥?one domain per tick. Keeps the
            // allowed-IP list pre-populated so first-access latency is zero
            // and CDN-rotated IPs stay current. Only relevant in whitelist mode.
            if (gatewayEnabled && gatewayDnsMode == DASH_DNS_WHITELIST &&
                gatewayDnsWhitelist.length() > 0 &&
                nowSec - gatewayWhitelistRefreshLastSec >= kDashGatewayWhitelistRefreshIntervalSec)
            {
                String rule = dashGatewayGetRuleByIndex(gatewayDnsWhitelist, gatewayWhitelistRefreshIdx);
                if (rule.length() == 0)
                {
                    gatewayWhitelistRefreshIdx = 0;
                    rule = dashGatewayGetRuleByIndex(gatewayDnsWhitelist, 0);
                }
                if (rule.length() > 0)
                {
                    uint16_t proxyId = gatewayNextProxyId++;
                    size_t qlen = dashGatewayBuildDnsQuery(rule, proxyId, tx, sizeof(tx));
                    if (qlen > 0)
                    {
                        uint32_t upstream = gatewayUpstreamDns != IPADDR_NONE ? gatewayUpstreamDns : inet_addr("223.6.6.6");
                        sockaddr_in dst = {};
                        dst.sin_family = AF_INET;
                        dst.sin_port = htons(53);
                        dst.sin_addr.s_addr = upstream;
                        ssize_t sent = sendto(gatewayUpstreamSock, tx, qlen, 0, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
                        if (sent == static_cast<ssize_t>(qlen))
                        {
                            DASH_GATEWAY_FOR_PENDING(q)
                            {
                                if (!q.inUse)
                                {
                                    dashGatewayInitPending(q, 0, proxyId, kDashGatewayDnsTypeA, nullptr, rule, false, true);
                                    break;
                                }
                            }
                        }
                    }
                    gatewayWhitelistRefreshIdx++;
                    gatewayWhitelistRefreshLastSec = nowSec;
                }
            }
        }

        if (FD_ISSET(gatewayDnsSock, &rfds))
        {
            sockaddr_in client = {};
            socklen_t clientLen = sizeof(client);
            int n = recvfrom(gatewayDnsSock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&client), &clientLen);
            if (n >= 12)
            {
                String qname;
                uint16_t qtype = 0;
                bool parsed = dashGatewayParseDnsQuestion(rx, n, qname, qtype);

                // IPv6 forwarding/filtering is intentionally not supported:
                // answer AAAA locally with no data so clients fall back to A.
                if (parsed && qtype == kDashGatewayDnsTypeAAAA)
                {
                    size_t len = dashGatewayMakeDnsBlockedReply(rx, n, tx, sizeof(tx));
                    if (len > 0)
                        sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                    continue;
                }

                // Special domain t.sl -> fake IP 100.100.1.1
                if (parsed && n >= 16 && (qname == "t.sl" || qname == "t.sl."))
                {
                    if (qtype == kDashGatewayDnsTypeA)
                    {
                        ESP_LOGI(kDashGatewayTag, "Fake response for %s -> 100.100.1.1", qname.c_str());
                        uint32_t fakeIp = PP_HTONL(LWIP_MAKEU32(100, 100, 1, 1));
                        size_t len = dashGatewayMakeDnsFakeReply(rx, n, tx, sizeof(tx), fakeIp);
                        if (len > 0)
                            sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                        continue;
                    }
                }

                bool allowed = !gatewayEnabled || !parsed || dashGatewayDnsAllowed(qname);
                if (!allowed)
                {
                    dashGatewayTrackBlocked(qname);
                    size_t len = dashGatewayMakeDnsBlockedReply(rx, n, tx, sizeof(tx));
                    if (len > 0)
                        sendto(gatewayDnsSock, tx, len, 0, reinterpret_cast<sockaddr *>(&client), clientLen);

                    // Self-learning IP blackhole: forward the blocked query to
                    // upstream so we can capture the real A-record IPs and add
                    // them to the NAT drop list. Catches hardcoded-IP bypass.
                    if (parsed && qname.length() > 0 && qtype == kDashGatewayDnsTypeA)
                    {
                        uint16_t origId = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
                        uint16_t proxyId = gatewayNextProxyId++;
                        rx[0] = proxyId >> 8; rx[1] = proxyId & 0xFF;
                        uint32_t upstream = gatewayUpstreamDns != IPADDR_NONE ? gatewayUpstreamDns : inet_addr("223.6.6.6");
                        sockaddr_in dst = {};
                        dst.sin_family = AF_INET;
                        dst.sin_port = htons(53);
                        dst.sin_addr.s_addr = upstream;
                        ssize_t sent = sendto(gatewayUpstreamSock, rx, n, 0, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
                        if (sent == n)
                        {
                            DASH_GATEWAY_FOR_PENDING(q)
                            {
                                if (!q.inUse)
                                {
                                    dashGatewayInitPending(q, origId, proxyId, qtype, nullptr, qname, true, false);
                                    break;
                                }
                            }
                        }
                    }
                }
                else
                {
                    uint16_t origId = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
                    if (parsed)
                    {
                        uint32_t nowSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
                        size_t cachedLen = dashGatewayDnsCacheLookup(qname, qtype, nowSec, tx, sizeof(tx));
                        if (cachedLen > 0)
                        {
                            tx[0] = origId >> 8; tx[1] = origId & 0xFF;
                            sendto(gatewayDnsSock, tx, cachedLen, 0, reinterpret_cast<sockaddr *>(&client), clientLen);
                            if (gatewayDnsMode == DASH_DNS_WHITELIST)
                                dashGatewayExtractAllowedIps(tx, static_cast<int>(cachedLen));
                            continue;
                        }
                    }
                    if (parsed && dashGatewayAttachDuplicatePending(qname, qtype, origId, client))
                        continue;
                    uint16_t proxyId = gatewayNextProxyId++;
                    rx[0] = proxyId >> 8; rx[1] = proxyId & 0xFF;
                    uint32_t upstream = gatewayUpstreamDns != IPADDR_NONE ? gatewayUpstreamDns : inet_addr("223.6.6.6");
                    sockaddr_in dst = {};
                    dst.sin_family = AF_INET;
                    dst.sin_port = htons(53);
                    dst.sin_addr.s_addr = upstream;
                    ssize_t sent = sendto(gatewayUpstreamSock, rx, n, 0, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
                    if (sent == n)
                    {
                        DASH_GATEWAY_FOR_PENDING(q)
                        {
                            if (!q.inUse)
                            {
                                dashGatewayInitPending(q, origId, proxyId, qtype, &client, qname, false, false);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (FD_ISSET(gatewayUpstreamSock, &rfds))
        {
            sockaddr_in from = {};
            socklen_t fromLen = sizeof(from);
            int rn = recvfrom(gatewayUpstreamSock, rx, sizeof(rx), 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
            if (rn > 0)
            {
                uint16_t respId = static_cast<uint16_t>((rx[0] << 8) | rx[1]);
                DASH_GATEWAY_FOR_PENDING(q)
                {
                    if (q.inUse && q.proxyId == respId)
                    {
                        if (q.rulesVersion != gatewayDnsRulesVersion)
                        {
                            q.inUse = false;
                            break;
                        }
                        if (q.blackholeLearn)
                        {
                            dashGatewayExtractBlackholeIps(rx, rn);
                        }
                        else if (q.whitelistRefresh)
                        {
                            // Background prefetch 鈥?extract allowed IPs only, no client.
                            dashGatewayExtractAllowedIps(rx, rn);
                            dashGatewayDnsCachePut(q.domain, q.qtype, static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL), rx, rn);
                        }
                        else
                        {
                            dashGatewayDnsCachePut(q.domain, q.qtype, static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL), rx, rn);
                            if (gatewayDnsMode == DASH_DNS_WHITELIST)
                                dashGatewayExtractAllowedIps(rx, rn);
                            for (uint8_t ci = 0; ci < q.clientCount; ci++)
                            {
                                rx[0] = q.clientIds[ci] >> 8; rx[1] = q.clientIds[ci] & 0xFF;
                                sendto(gatewayDnsSock, rx, rn, 0, reinterpret_cast<sockaddr *>(&q.clients[ci]), sizeof(q.clients[ci]));
                            }
                        }
                        q.inUse = false;
                        break;
                    }
                }
            }
        }
    }
}

static bool dashGatewayReadListFile(const char *path, String &out)
{
    if (!SPIFFS.exists(path))
        return false;
    File f = SPIFFS.open(path, "r");
    if (!f)
        return false;
    out = f.readString();
    f.close();
    return true;
}

static bool dashGatewayWriteListFile(const char *path, const String &value)
{
    File f = SPIFFS.open(path, "w");
    if (!f)
        return false;
    size_t written = f.write(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    f.close();
    return written == value.length();
}

static bool dashGatewaySaveMeta()
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kDashGatewayPrefsNs, NVS_READWRITE, &h);
    if (err != ESP_OK)
    {
        ESP_LOGW(kDashGatewayTag, "NVS open failed while saving gateway meta: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u8(h, "en", gatewayEnabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(h, "mode", static_cast<uint8_t>(gatewayDnsMode));
    if (err == ESP_OK) err = nvs_set_u8(h, "strict", gatewayDnsStrict ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(h, "profile", kDashGatewayTeslaProfileVersion);
    // Lists now live in SPIFFS so large edits no longer consume scarce NVS pages.
    if (err == ESP_OK)
    {
        esp_err_t eraseBlack = nvs_erase_key(h, "black");
        if (eraseBlack != ESP_OK && eraseBlack != ESP_ERR_NVS_NOT_FOUND)
            err = eraseBlack;
    }
    if (err == ESP_OK)
    {
        esp_err_t eraseWhite = nvs_erase_key(h, "white");
        if (eraseWhite != ESP_OK && eraseWhite != ESP_ERR_NVS_NOT_FOUND)
            err = eraseWhite;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK)
    {
        ESP_LOGW(kDashGatewayTag, "NVS commit failed while saving gateway meta: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool dashGatewaySave()
{
    bool ok = dashGatewayWriteListFile(kDashGatewayBlacklistPath, gatewayDnsBlacklist);
    ok = dashGatewayWriteListFile(kDashGatewayWhitelistPath, gatewayDnsWhitelist) && ok;
    ok = dashGatewayWriteListFile(kDashGatewayCidrPath, gatewayDnsCidrAllowlist) && ok;
    ok = dashGatewaySaveMeta() && ok;
    if (!ok)
        ESP_LOGW(kDashGatewayTag, "Gateway DNS settings save failed");
    return ok;
}

static void dashGatewayLoad()
{
    if (!dashGatewayAllocateState())
    {
        gatewayEnabled = false;
        ESP_LOGE(kDashGatewayTag, "Gateway disabled because DNS state allocation failed");
        return;
    }
    String legacyBlacklist = kDashGatewayDefaultBlacklist;
    String legacyWhitelist = kDashGatewayDefaultWhitelist;
    uint8_t profileVersion = 0;
    Preferences p;
    if (p.begin(kDashGatewayPrefsNs, false))
    {
        gatewayEnabled = p.getBool("en", true);
        gatewayDnsMode = static_cast<DashGatewayDnsMode>(p.getUChar("mode", DASH_DNS_BLACKLIST) ? DASH_DNS_WHITELIST : DASH_DNS_BLACKLIST);
        gatewayDnsStrict = p.getBool("strict", false);
        profileVersion = p.getUChar("profile", 0);
        legacyBlacklist = p.getString("black", kDashGatewayDefaultBlacklist);
        legacyWhitelist = p.getString("white", kDashGatewayDefaultWhitelist);
        p.end();
    }

    String fileList;
    bool loadedBlackFromFile = dashGatewayReadListFile(kDashGatewayBlacklistPath, fileList);
    gatewayDnsBlacklist = dashGatewaySanitizeBlacklist(loadedBlackFromFile ? fileList : legacyBlacklist);
    bool loadedWhiteFromFile = dashGatewayReadListFile(kDashGatewayWhitelistPath, fileList);
    gatewayDnsWhitelist = dashGatewaySanitizeWhitelist(loadedWhiteFromFile ? fileList : legacyWhitelist);
    bool loadedCidrFromFile = dashGatewayReadListFile(kDashGatewayCidrPath, fileList);
    gatewayDnsCidrAllowlist = dashGatewaySanitizeCidrAllowlist(loadedCidrFromFile ? fileList : String(""));

    if (profileVersion < kDashGatewayTeslaProfileVersion)
    {
        gatewayEnabled = true;
        gatewayDnsMode = DASH_DNS_BLACKLIST;
        gatewayDnsStrict = false;
        gatewayDnsBlacklist = dashGatewaySanitizeBlacklist(kDashGatewayDefaultBlacklist);
        gatewayDnsWhitelist = dashGatewaySanitizeWhitelist(kDashGatewayDefaultWhitelist);
        gatewayDnsCidrAllowlist = "";
        gatewayDnsRulesVersion++;
        dashGatewayDnsCacheClear();
        loadedBlackFromFile = loadedWhiteFromFile = loadedCidrFromFile = false;
    }

    dashGatewayCompileAllRules();

    if (!loadedBlackFromFile || !loadedWhiteFromFile || !loadedCidrFromFile)
        dashGatewaySave();
}

static void dashGatewayStartDns()
{
    if (gatewayDnsTaskHandle)
        return;
    gatewayDnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gatewayDnsSock < 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS socket open failed");
        return;
    }
    int yes = 1;
    setsockopt(gatewayDnsSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(gatewayDnsSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        ESP_LOGW(kDashGatewayTag, "DNS bind failed (errno=%d)", errno);
        close(gatewayDnsSock);
        gatewayDnsSock = -1;
        gatewayDnsBindOk = false;
        return;
    }
    gatewayDnsBindOk = true;
    ESP_LOGI(kDashGatewayTag, "DNS socket bound to UDP 53 (fd=%d)", gatewayDnsSock);
    xTaskCreatePinnedToCore(dashGatewayDnsTask, "gw_dns", 6144, nullptr, 1, &gatewayDnsTaskHandle, 1);
}

static bool gatewayApDnsConfigured = false; // guard: only configure DHCP/DNS once

static void dashGatewayConfigureApDns(esp_netif_t *apNetif)
{
    if (!apNetif)
        return;
    // Only stop/start DHCP server once. Re-running this disrupts AP clients
    // that already have a lease (forces re-DHCP). Call only at first AP start.
    if (gatewayApDnsConfigured)
    {
        ESP_LOGI(kDashGatewayTag, "AP DNS already configured, skipping DHCP restart");
        return;
    }
    esp_netif_ip_info_t apIp = {};
    if (esp_netif_get_ip_info(apNetif, &apIp) != ESP_OK)
        return;
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = apIp.ip;
    esp_netif_dhcps_stop(apNetif);
    esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dns);
    dhcps_offer_t offer = OFFER_DNS;
    esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(apNetif);
    gatewayApDnsConfigured = true;
    ESP_LOGI(kDashGatewayTag, "AP DNS configured -> " IPSTR, IP2STR(&apIp.ip));
}

static void dashGatewayOnApStarted(esp_netif_t *apNetif)
{
    if (!gatewayEnabled)
        return;
    dashGatewayConfigureApDns(apNetif);
    dashGatewayStartDns();
    ESP_LOGI(kDashGatewayTag, "AP ready ip=%s clients=%u dns=%s",
             WiFi.softAPIP().toString().c_str(),
             static_cast<unsigned>(WiFi.softAPgetStationNum()),
             gatewayApDnsConfigured ? "configured" : "pending");
}

static void dashGatewayOnStaConnected(esp_netif_t *staNetif, esp_netif_t *apNetif)
{
    if (!gatewayEnabled)
        return;
    esp_netif_dns_info_t dns = {};
    if (staNetif && esp_netif_get_dns_info(staNetif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4 && dns.ip.u_addr.ip4.addr != 0)
    {
        gatewayUpstreamDns = dns.ip.u_addr.ip4.addr;
    }

#if IP_NAPT
    void *lwipAp = apNetif ? esp_netif_get_netif_impl(apNetif) : nullptr;
    if (lwipAp)
    {
        if (ip_napt_enable_netif(static_cast<netif *>(lwipAp), 1))
            gatewayNaptEnabled = true;
        else
            ESP_LOGW(kDashGatewayTag, "ip_napt_enable_netif failed");
    }
#else
    ESP_LOGW(kDashGatewayTag, "CONFIG_LWIP_IPV4_NAPT is disabled");
#endif
    char upstreamLog[16] = "none";
    if (gatewayUpstreamDns != IPADDR_NONE && gatewayUpstreamDns != 0)
    {
        struct in_addr a;
        a.s_addr = gatewayUpstreamDns;
        const char *p = inet_ntoa(a);
        if (p)
            snprintf(upstreamLog, sizeof(upstreamLog), "%s", p);
    }
    ESP_LOGI(kDashGatewayTag, "STA ready ip=%s upstream_dns=%s nat=%s",
             WiFi.localIP().toString().c_str(),
             upstreamLog,
             gatewayNaptEnabled ? "on" : "waiting");
}

static String dashGatewayStatusJson()
{
    // Format upstream DNS as dotted-decimal string
    char upstreamStr[16] = "none";
    if (gatewayUpstreamDns != IPADDR_NONE && gatewayUpstreamDns != 0)
    {
        // gatewayUpstreamDns is stored in network byte order (as returned by
        // esp_netif_get_dns_info -> ip4.addr which is lwIP NBO). Use inet_ntoa.
        struct in_addr a;
        a.s_addr = gatewayUpstreamDns;
        const char *p = inet_ntoa(a);
        if (p)
            snprintf(upstreamStr, sizeof(upstreamStr), "%s", p);
    }

    String j = "{\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"nat\":";
    j += gatewayNaptEnabled ? "true" : "false";
#if IP_NAPT
    j += ",\"napt_compiled\":true";
#else
    j += ",\"napt_compiled\":false";
#endif
    j += ",\"ap_ip\":\"";
    j += WiFi.softAPIP().toString();
    j += "\",\"ap_clients\":";
    j += String(WiFi.softAPgetStationNum());
    j += ",\"sta_connected\":";
    j += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
    j += ",\"sta_ip\":\"";
    j += WiFi.localIP().toString();
    j += "\",\"sta_ssid\":\"";
    j += jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""));
    j += "\"";
    j += ",\"mode\":";
    j += String(static_cast<int>(gatewayDnsMode));
    j += ",\"strict\":";
    j += gatewayDnsStrict ? "true" : "false";
    j += ",\"blocked\":";
    j += String(gatewayBlockedDomainCount);
    j += ",\"blocked_ips\":";
    j += String(gatewayBlockedIpCount);
    j += ",\"allowed_ips\":";
    j += String(gatewayAllowedIpCount);
    j += ",\"dns_cache_psram\":";
    j += (gatewayPendingQueriesInPsram || gatewayAllowedIpsInPsram || gatewayBlockedIpsInPsram || gatewayDnsCacheInPsram) ? "true" : "false";
    j += ",\"dns_resp_cache\":";
    j += gatewayDnsCache ? String(static_cast<unsigned>(kDashGatewayDnsCacheEntries)) : "0";
    j += ",\"dns_resp_hits\":";
    j += String(gatewayDnsCacheHits);
    j += ",\"dns_resp_misses\":";
    j += String(gatewayDnsCacheMisses);
    j += ",\"black_rules\":";
    j += String(gatewayBlacklistRuleCount);
    j += ",\"white_rules\":";
    j += String(gatewayWhitelistRuleCount);
    j += ",\"cidr_rules\":";
    j += String(gatewayCidrRuleCount);
    j += ",\"rules_version\":";
    j += String(gatewayDnsRulesVersion);
    // DNS health observability fields
    j += ",\"dns_bind_ok\":";
    j += gatewayDnsBindOk ? "true" : "false";
    j += ",\"dns_task_active\":";
    j += (gatewayDnsTaskHandle != nullptr) ? "true" : "false";
    j += ",\"dns_sock\":";
    j += String(gatewayDnsSock);
    j += ",\"upstream_dns\":\"";
    j += upstreamStr;
    j += "\",\"ap_dns_configured\":";
    j += gatewayApDnsConfigured ? "true" : "false";
    j += "}";
    return j;
}

static void handleGatewayStatus()
{
    server.send(200, "application/json", dashGatewayStatusJson());
}

static String dashGatewayDnsSettingsJson(bool ok)
{
    String j = "{\"ok\":";
    j += ok ? "true" : "false";
    j += ",\"enabled\":";
    j += gatewayEnabled ? "true" : "false";
    j += ",\"mode\":";
    j += String(static_cast<int>(gatewayDnsMode));
    j += ",\"strict\":";
    j += gatewayDnsStrict ? "true" : "false";
    j += ",\"blacklist\":\"";
    j += jsonEscape(gatewayDnsBlacklist.c_str());
    j += "\",\"whitelist\":\"";
    j += jsonEscape(gatewayDnsWhitelist.c_str());
    j += "\",\"cidr\":\"";
    j += jsonEscape(gatewayDnsCidrAllowlist.c_str());
    j += "\",\"black_count\":";
    j += String(static_cast<unsigned>(dashGatewayCountEntries(gatewayDnsBlacklist)));
    j += ",\"white_count\":";
    j += String(static_cast<unsigned>(dashGatewayCountEntries(gatewayDnsWhitelist)));
    j += ",\"cidr_count\":";
    j += String(static_cast<unsigned>(dashGatewayCountEntries(gatewayDnsCidrAllowlist)));
    j += ",\"black_max\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxBlacklistEntries));
    j += ",\"white_max\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxWhitelistEntries));
    j += ",\"cidr_max\":";
    j += String(static_cast<unsigned>(kDashGatewayMaxCidrEntries));
    j += "}";
    return j;
}

static void handleGatewayDnsGet()
{
    String j = dashGatewayDnsSettingsJson(true);
    server.send(200, "application/json", j);
}

static void handleGatewayDnsTest()
{
    String domain = server.hasArg("domain") ? server.arg("domain") : "";
    server.send(200, "application/json", dashGatewayDnsDecisionJson(domain));
}

static void handleGatewayDnsPost()
{
    bool oldEnabled = gatewayEnabled;
    DashGatewayDnsMode oldMode = gatewayDnsMode;
    bool oldStrict = gatewayDnsStrict;
    String oldBlacklist = gatewayDnsBlacklist;
    String oldWhitelist = gatewayDnsWhitelist;
    String oldCidr = gatewayDnsCidrAllowlist;

    gatewayEnabled = !server.hasArg("enabled") || server.arg("enabled").toInt() != 0;
    gatewayDnsMode = server.hasArg("mode") && server.arg("mode").toInt() == 0 ? DASH_DNS_BLACKLIST : DASH_DNS_WHITELIST;
    gatewayDnsStrict = server.hasArg("strict") && server.arg("strict").toInt() != 0;
    bool rulesChanged = false;
    if (server.hasArg("blacklist"))
    {
        String next = dashGatewaySanitizeBlacklist(server.arg("blacklist"));
        if (next != gatewayDnsBlacklist) { gatewayDnsBlacklist = next; rulesChanged = true; }
    }
    if (server.hasArg("whitelist"))
    {
        String next = dashGatewaySanitizeWhitelist(server.arg("whitelist"));
        if (next != gatewayDnsWhitelist) { gatewayDnsWhitelist = next; rulesChanged = true; }
    }
    if (server.hasArg("cidr"))
    {
        String next = dashGatewaySanitizeCidrAllowlist(server.arg("cidr"));
        if (next != gatewayDnsCidrAllowlist) { gatewayDnsCidrAllowlist = next; rulesChanged = true; }
    }
    if (!dashGatewaySave())
    {
        gatewayEnabled = oldEnabled;
        gatewayDnsMode = oldMode;
        gatewayDnsStrict = oldStrict;
        gatewayDnsBlacklist = oldBlacklist;
        gatewayDnsWhitelist = oldWhitelist;
        gatewayDnsCidrAllowlist = oldCidr;
        dashGatewayCompileAllRules();
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }
    // Rule change invalidates auto-learned IP caches: an IP that was a blackhole
    // entry might now belong to a freshly-allowed domain (and vice versa). Clear
    // both caches so the next DNS round repopulates with current semantics.
    if (rulesChanged)
    {
        gatewayDnsRulesVersion++;
        dashGatewayCompileAllRules();
        dashGatewayDnsCacheClear();
        portENTER_CRITICAL(&gatewayBlockedIpMux);
        std::memset(gatewayBlockedIps, 0, sizeof(DashGatewayBlockedIp) * kDashGatewayMaxBlockedIps);
        gatewayBlockedIpCount = 0;
        portEXIT_CRITICAL(&gatewayBlockedIpMux);
        portENTER_CRITICAL(&gatewayAllowedMux);
        std::memset(gatewayAllowedIps, 0, sizeof(DashGatewayAllowedIp) * kDashGatewayMaxAllowedIps);
        gatewayAllowedIpCount = 0;
        portEXIT_CRITICAL(&gatewayAllowedMux);
    }
    else if (oldEnabled != gatewayEnabled || oldMode != gatewayDnsMode || oldStrict != gatewayDnsStrict)
    {
        gatewayDnsRulesVersion++;
    }
    server.send(200, "application/json", dashGatewayDnsSettingsJson(true));
}

static void handleGatewayWhitelistAdd()
{
    if (!server.hasArg("domain"))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"domain required\"}");
        return;
    }
    String domain = dashGatewayNormalizeDomain(server.arg("domain"));
    if (dashGatewayDomainInList(domain, gatewayDnsBlacklist))
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"domain is blacklisted\"}");
        return;
    }
    if (dashGatewayDomainInList(domain, gatewayDnsWhitelist))
    {
        server.send(200, "application/json", "{\"ok\":true,\"already\":true}");
        return;
    }
    if (dashGatewayCountEntries(gatewayDnsWhitelist) >= kDashGatewayMaxWhitelistEntries)
    {
        server.send(409, "application/json", "{\"ok\":false,\"error\":\"whitelist full (max 200)\"}");
        return;
    }
    String oldWhitelist = gatewayDnsWhitelist;
    if (!dashGatewayAppendWhitelist(domain))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"cannot add domain\"}");
        return;
    }
    if (!dashGatewaySave())
    {
        gatewayDnsWhitelist = oldWhitelist;
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
        return;
    }
    gatewayDnsRulesVersion++;
    dashGatewayCompileAllRules();
    dashGatewayDnsCacheClear();
    server.send(200, "application/json", dashGatewayDnsSettingsJson(true));
}

static void handleGatewayBlocked()
{
    // Snapshot under lock 鈥?never call String/heap ops inside portENTER_CRITICAL.
    DashGatewayBlockedDomain snapshot[32];
    uint8_t count;
    portENTER_CRITICAL(&gatewayBlockedMux);
    count = gatewayBlockedDomainCount;
    if (count > 32) count = 32;
    for (uint8_t i = 0; i < count; i++)
        snapshot[i] = gatewayBlockedDomains[i];
    portEXIT_CRITICAL(&gatewayBlockedMux);

    String j = "[";
    for (uint8_t i = 0; i < count; i++)
    {
        if (i) j += ",";
        j += "{\"domain\":\"";
        j += jsonEscape(snapshot[i].domain);
        j += "\",\"count\":";
        j += String(snapshot[i].count);
        j += ",\"blacklisted\":";
        j += dashGatewayDomainInList(snapshot[i].domain, gatewayDnsBlacklist) ? "true" : "false";
        j += ",\"whitelisted\":";
        j += dashGatewayDomainInList(snapshot[i].domain, gatewayDnsWhitelist) ? "true" : "false";
        j += ",\"canWhitelist\":";
        j += dashGatewayDomainAllowedForWhitelist(snapshot[i].domain) ? "true" : "false";
        j += "}";
    }
    j += "]";
    server.send(200, "application/json", j);
}

static void handleGatewayBlockedClear()
{
    portENTER_CRITICAL(&gatewayBlockedMux);
    gatewayBlockedDomainCount = 0;
    std::memset(gatewayBlockedDomains, 0, sizeof(DashGatewayBlockedDomain) * 32);
    portEXIT_CRITICAL(&gatewayBlockedMux);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGatewayBlockedIps()
{
    String j = "[";
    bool first = true;
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    for (uint8_t i = 0; i < kDashGatewayMaxBlockedIps; i++)
    {
        if (gatewayBlockedIps[i].ip == 0)
            continue;
        if (!first) j += ",";
        first = false;
        uint32_t ip = gatewayBlockedIps[i].ip;
        char ipStr[20];
        std::snprintf(ipStr, sizeof(ipStr), "%u.%u.%u.%u",
            static_cast<unsigned>(ip & 0xFF),
            static_cast<unsigned>((ip >> 8) & 0xFF),
            static_cast<unsigned>((ip >> 16) & 0xFF),
            static_cast<unsigned>((ip >> 24) & 0xFF));
        j += "{\"ip\":\"";
        j += ipStr;
        j += "\",\"count\":";
        j += String(gatewayBlockedIps[i].count);
        j += "}";
    }
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
    j += "]";
    server.send(200, "application/json", j);
}

static void handleGatewayBlockedIpsClear()
{
    portENTER_CRITICAL(&gatewayBlockedIpMux);
    gatewayBlockedIpCount = 0;
    std::memset(gatewayBlockedIps, 0, sizeof(DashGatewayBlockedIp) * kDashGatewayMaxBlockedIps);
    portEXIT_CRITICAL(&gatewayBlockedIpMux);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGatewayStrict()
{
    if (server.hasArg("enabled"))
    {
        bool oldStrict = gatewayDnsStrict;
        gatewayDnsStrict = server.arg("enabled").toInt() != 0;
        if (oldStrict != gatewayDnsStrict)
            gatewayDnsRulesVersion++;
        if (!gatewayDnsStrict)
        {
            portENTER_CRITICAL(&gatewayBlockedIpMux);
            gatewayBlockedIpCount = 0;
            std::memset(gatewayBlockedIps, 0, sizeof(DashGatewayBlockedIp) * kDashGatewayMaxBlockedIps);
            portEXIT_CRITICAL(&gatewayBlockedIpMux);
        }
        dashGatewaySave();
    }
    String j = "{\"strict\":";
    j += gatewayDnsStrict ? "true" : "false";
    j += ",\"allowed_count\":";
    j += String(gatewayAllowedIpCount);
    j += ",\"blocked_count\":";
    j += String(gatewayBlockedIpCount);
    j += "}";
    server.send(200, "application/json", j);
}

#else

static void dashGatewayLoad() {}
static void dashGatewayOnApStarted(esp_netif_t *) {}
static void dashGatewayOnStaConnected(esp_netif_t *, esp_netif_t *) {}

#endif

