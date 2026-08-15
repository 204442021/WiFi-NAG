from pathlib import Path
import subprocess

from SCons.Errors import UserError
from SCons.Script import Import

Import("env")


CREDENTIAL_DEFINES = ("DASH_SSID", "DASH_PASS", "DASH_OTA_USER", "DASH_OTA_PASS")
CONFIG_RELATIVE_PATH = Path("platformio_profile.h")
EXAMPLE_CONFIG_RELATIVE_PATH = Path("platformio_profile.example.h")

# PlatformIO retains sdkconfig.<environment> after the first configure. Kconfig
# defaults do not override values already stored there, so keep the BLE bridge
# symbols synchronized before ESP-IDF evaluates the project configuration.
BLE_SDKCONFIG_LINES = (
    "CONFIG_BT_ENABLED=y",
    "CONFIG_BT_CONTROLLER_ENABLED=y",
    "CONFIG_BT_BLUEDROID_ENABLED=n",
    "CONFIG_BT_NIMBLE_ENABLED=y",
    "CONFIG_BT_NIMBLE_ROLE_CENTRAL=y",
    "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n",
    "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n",
    "CONFIG_BT_NIMBLE_ROLE_OBSERVER=n",
    "CONFIG_BT_NIMBLE_GATT_CLIENT=y",
    "CONFIG_BT_NIMBLE_GATT_SERVER=n",
    "CONFIG_BT_NIMBLE_SECURITY_ENABLE=y",
    "CONFIG_BT_NIMBLE_SM_LEGACY=n",
    "CONFIG_BT_NIMBLE_SM_SC=y",
    "CONFIG_BT_NIMBLE_SM_SC_ONLY=1",
    "CONFIG_BT_NIMBLE_SM_LVL=1",
    "CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION=y",
    "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
    "CONFIG_BT_NIMBLE_HANDLE_REPEAT_PAIRING_DELETION=n",
    "CONFIG_BT_NIMBLE_MAX_BONDS=2",
    "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
    "CONFIG_BT_NIMBLE_MAX_CCCDS=4",
    "CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=23",
    "CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096",
    "CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=n",
    "CONFIG_BT_NIMBLE_PINNED_TO_CORE_1=y",
    "CONFIG_BT_NIMBLE_PINNED_TO_CORE=1",
    "CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y",
    "CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=y",
    "CONFIG_BT_NIMBLE_MAX_CONN_REATTEMPT=3",
    "CONFIG_BT_NIMBLE_HOST_QUEUE_CONG_CHECK=y",
    "CONFIG_BT_NIMBLE_USE_ESP_TIMER=y",
    "CONFIG_BT_CTRL_BLE_MAX_ACT=3",
    "CONFIG_BT_CTRL_PINNED_TO_CORE_0=n",
    "CONFIG_BT_CTRL_PINNED_TO_CORE_1=y",
    "CONFIG_BT_CTRL_PINNED_TO_CORE=1",
    "CONFIG_BT_CTRL_HCI_MODE_VHCI=y",
    "CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y",
)


def _string_define_values(text, names):
    result = {}
    for line in text.splitlines():
        stripped = line.lstrip()
        if not stripped.startswith("#define"):
            continue
        parts = stripped.split(None, 2)
        if len(parts) < 3 or parts[1] not in names:
            continue
        rest = parts[2].strip()
        if not rest.startswith('"'):
            continue
        end = rest.find('"', 1)
        if end != -1:
            result[parts[1]] = rest[1:end]
    return result


def _sync_ble_sdkconfig(project_dir, environment):
    if environment != "wifi_nag_ESP32_S3_CAN":
        return

    sdkconfig_path = project_dir / f"sdkconfig.{environment}"
    if not sdkconfig_path.exists():
        return

    desired_symbols = {line.split("=", 1)[0] for line in BLE_SDKCONFIG_LINES}
    marker = "# WIFI-NAG BLE bridge synchronized by scripts/platformio_sync_profile.py"
    retained = []
    for line in sdkconfig_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped == marker:
            continue
        if stripped.startswith("# CONFIG_") and stripped.endswith(" is not set"):
            symbol = stripped[2 : -len(" is not set")]
            if symbol in desired_symbols:
                continue
        if stripped.startswith("CONFIG_") and "=" in stripped:
            symbol = stripped.split("=", 1)[0]
            if symbol in desired_symbols:
                continue
        retained.append(line)

    while retained and not retained[-1].strip():
        retained.pop()
    retained.extend(["", marker, *BLE_SDKCONFIG_LINES, ""])
    next_text = "\n".join(retained)
    current_text = sdkconfig_path.read_text(encoding="utf-8")
    if current_text != next_text:
        sdkconfig_path.write_text(next_text, encoding="utf-8")
        print(f"Synchronized BLE bridge Kconfig in {sdkconfig_path.name}")


project_dir = Path(env["PROJECT_DIR"])
config_path = Path(env.GetProjectOption("custom_profile_path", CONFIG_RELATIVE_PATH.as_posix()))
if not config_path.is_absolute():
    config_path = project_dir / config_path

example_config_path = Path(
    env.GetProjectOption("custom_example_profile_path", EXAMPLE_CONFIG_RELATIVE_PATH.as_posix())
)
if not example_config_path.is_absolute():
    example_config_path = project_dir / example_config_path

version_path = Path(env.GetProjectOption("custom_version_path", "VERSION"))
if not version_path.is_absolute():
    version_path = project_dir / version_path

display_config_path = (
    config_path.relative_to(project_dir) if config_path.is_relative_to(project_dir) else config_path
)
display_example_path = (
    example_config_path.relative_to(project_dir)
    if example_config_path.is_relative_to(project_dir)
    else example_config_path
)

if not config_path.exists():
    raise UserError(
        f"Missing {display_config_path.as_posix()}. Copy "
        f"{display_example_path.as_posix()} to {display_config_path.as_posix()}, "
        "then edit local dashboard credentials."
    )

config_text = config_path.read_text(encoding="utf-8")

# Make platformio_profile.h resolvable via #include "platformio_profile.h" if
# local code or future scripts need it.
env.Append(CPPPATH=[str(config_path.parent)])

credentials = _string_define_values(config_text, CREDENTIAL_DEFINES)
for cred_name in CREDENTIAL_DEFINES:
    if cred_name in credentials:
        env.Append(CPPDEFINES=[(cred_name, f'\\"{credentials[cred_name]}\\"')])

if version_path.exists():
    fw_version = version_path.read_text(encoding="utf-8").strip()
    env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{fw_version}\\"')])

try:
    git_sha = subprocess.check_output(
        ["git", "rev-parse", "--short=7", "HEAD"],
        cwd=project_dir,
        text=True,
        stderr=subprocess.DEVNULL,
    ).strip()
except (OSError, subprocess.CalledProcessError):
    git_sha = "unknown"
env.Append(CPPDEFINES=[("FIRMWARE_GIT_SHA", f'\\"{git_sha}\\"')])

_sync_ble_sdkconfig(project_dir, env["PIOENV"])
print(f"Synced {display_config_path.as_posix()} WIFI-NAG credentials for {env['PIOENV']}")
