#pragma once

// Generated dashboard payload.  scripts/minify_dashboard.py writes this base
// file so this small integration wrapper remains stable across UI regeneration.
#include "web/mcp2515_dashboard_ui.base.h"

#ifdef ESP_PLATFORM
#include "web/nag_sweep_dashboard.h"

// mcp2515_dashboard.h declares its server after including this wrapper.  Use
// the compatible extension there without changing the rest of the dashboard.
#define WebServer NagSweepWebServer
#endif
