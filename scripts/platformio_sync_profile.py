from pathlib import Path

from SCons.Script import Import

Import("env")

project_dir = Path(env["PROJECT_DIR"])
version_path = Path(env.GetProjectOption("custom_version_path", "VERSION"))
if not version_path.is_absolute():
    version_path = project_dir / version_path

if version_path.exists():
    firmware_version = version_path.read_text(encoding="utf-8").strip()
    if firmware_version:
        env.Append(CPPDEFINES=[("FIRMWARE_VERSION", f'\\"{firmware_version}\\"')])

print(f"Synced WIFI-NAG firmware version for {env['PIOENV']}")
