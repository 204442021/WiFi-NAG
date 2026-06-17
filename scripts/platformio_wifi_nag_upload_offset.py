Import("env")

env.Replace(ESP32_APP_OFFSET="0x20000")
env["INTEGRATION_EXTRA_DATA"].update({"application_offset": "0x20000"})
