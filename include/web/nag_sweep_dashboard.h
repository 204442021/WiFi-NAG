#pragma once

#ifdef ESP_PLATFORM

#include "handlers.h"

// Extends the compatibility WebServer with persisted sweep-range API routes.
class NagSweepWebServer : public WebServer
{
public:
    explicit NagSweepWebServer(uint16_t port) : WebServer(port) {}

    void begin()
    {
        loadSweepPrefs();
        this->on("/api/nag-sweep", HTTP_GET, [this]() { handleSweepRequest(false); });
        this->on("/api/nag-sweep", HTTP_POST, [this]() { handleSweepRequest(true); });
        WebServer::begin();
    }

private:
    static constexpr const char *kPrefsNamespace = "ADunlock";
    static constexpr const char *kMinKey = "nag_sw_min";
    static constexpr const char *kMaxKey = "nag_sw_max";

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
};

#endif
