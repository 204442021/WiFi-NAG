from pathlib import Path

from SCons.Errors import UserError
from SCons.Script import Import

Import("env")


CREDENTIAL_DEFINES = ("DASH_SSID", "DASH_PASS", "DASH_OTA_USER", "DASH_OTA_PASS")
CONFIG_RELATIVE_PATH = Path("platformio_profile.h")
EXAMPLE_CONFIG_RELATIVE_PATH = Path("platformio_profile.example.h")


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

print(f"Synced {display_config_path.as_posix()} WIFI-NAG credentials for {env['PIOENV']}")
