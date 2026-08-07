#pragma once

#ifdef ESP_PLATFORM

#include <cstdlib>
#include <cstring>
#include "handlers.h"
#include "web/nag_sweep_ui_patch.h"

// Extends the existing compatibility WebServer without changing its public
// dashboard API.  It adds a small persisted sweep-range endpoint and appends a
// gzip-compressed UI patch to the generated dashboard page.
class NagSweepWebServer : public WebServer
{
public:
    explicit NagSweepWebServer(uint16_t port) : WebServer(port) {}

    ~NagSweepWebServer()
    {
        if (patchedHtml_)
            std::free(patchedHtml_);
    }

    void begin()
    {
        loadSweepPrefs();
        this->on("/api/nag-sweep", HTTP_GET, [this]() { handleSweepRequest(false); });
        this->on("/api/nag-sweep", HTTP_POST, [this]() { handleSweepRequest(true); });
        WebServer::begin();
    }

    void sendRaw(int code, const char *type, const char *body, size_t len)
    {
        if (!body || !type || std::strcmp(type, "text/html") != 0 ||
            NAG_SWEEP_UI_PATCH_GZ_LEN == 0 || !preparePatchedHtml(body, len))
        {
            WebServer::sendRaw(code, type, body, len);
            return;
        }

        WebServer::sendRaw(code, type, reinterpret_cast<const char *>(patchedHtml_), patchedHtmlLen_);
    }

private:
    static constexpr const char *kPrefsNamespace = "ADunlock";
    static constexpr const char *kMinKey = "nag_sw_min";
    static constexpr const char *kMaxKey = "nag_sw_max";

    uint8_t *patchedHtml_ = nullptr;
    size_t patchedHtmlLen_ = 0;
    const char *patchedSource_ = nullptr;
    size_t patchedSourceLen_ = 0;

    void loadSweepPrefs()
    {
        Preferences sweepPrefs;
        int minSeconds = kNagSweepDefaultMinSeconds;
        int maxSeconds = kNagSweepDefaultMaxSeconds;
        const bool opened = sweepPrefs.begin(kPrefsNamespace, false);
        if (opened)
        {
            minSeconds = sweepPrefs.getUChar(kMinKey, kNagSweepDefaultMinSeconds);
            maxSeconds = sweepPrefs.getUChar(kMaxKey, kNagSweepDefaultMaxSeconds);
        }

        nagSetSweepRangeSeconds(minSeconds, maxSeconds);

        if (opened)
        {
            // Also writes explicit defaults on first boot, giving this setting
            // one stable NVS source of truth across later firmware updates.
            sweepPrefs.putUChar(kMinKey, nagSweepMinSecondsValue());
            sweepPrefs.putUChar(kMaxKey, nagSweepMaxSecondsValue());
            sweepPrefs.end();
        }
    }

    void saveSweepPrefs()
    {
        Preferences sweepPrefs;
        if (!sweepPrefs.begin(kPrefsNamespace, false))
            return;
        sweepPrefs.putUChar(kMinKey, nagSweepMinSecondsValue());
        sweepPrefs.putUChar(kMaxKey, nagSweepMaxSecondsValue());
        sweepPrefs.end();
    }

    void handleSweepRequest(bool update)
    {
        if (update)
        {
            int minSeconds = nagSweepMinSecondsValue();
            int maxSeconds = nagSweepMaxSecondsValue();
            if (this->hasArg("minSec"))
                minSeconds = this->arg("minSec").toInt();
            if (this->hasArg("maxSec"))
                maxSeconds = this->arg("maxSec").toInt();
            nagSetSweepRangeSeconds(minSeconds, maxSeconds);
            saveSweepPrefs();
        }

        String json = "{\"ok\":true,\"minSec\":";
        json += String((unsigned int)nagSweepMinSecondsValue());
        json += ",\"maxSec\":";
        json += String((unsigned int)nagSweepMaxSecondsValue());
        json += "}";
        this->send(200, "application/json", json);
    }

    bool preparePatchedHtml(const char *body, size_t len)
    {
        if (patchedHtml_ && patchedSource_ == body && patchedSourceLen_ == len)
            return true;

        if (patchedHtml_)
        {
            std::free(patchedHtml_);
            patchedHtml_ = nullptr;
        }

        const size_t combinedLen = len + NAG_SWEEP_UI_PATCH_GZ_LEN;
        uint8_t *combined = static_cast<uint8_t *>(std::malloc(combinedLen));
        if (!combined)
        {
            patchedHtmlLen_ = 0;
            patchedSource_ = nullptr;
            patchedSourceLen_ = 0;
            return false;
        }

        std::memcpy(combined, body, len);
        std::memcpy(combined + len, NAG_SWEEP_UI_PATCH_GZ, NAG_SWEEP_UI_PATCH_GZ_LEN);
        patchedHtml_ = combined;
        patchedHtmlLen_ = combinedLen;
        patchedSource_ = body;
        patchedSourceLen_ = len;
        return true;
    }
};

#endif
