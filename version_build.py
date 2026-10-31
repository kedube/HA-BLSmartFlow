"""Reserve a new major.feature.build version for each firmware build."""
from datetime import date
from pathlib import Path
import re


def find_version(config, pioenv):
    section = re.search(
        r"(?m)^\[env:" + re.escape(pioenv) + r"\][^\n]*\n(?:(?!^\[)[\s\S])*",
        config,
    )
    if not section:
        raise ValueError("Missing firmware environment in platformio.ini")
    match = re.search(r"(?m)^(custom_version\s*=\s*)(\d+)\.(\d+)\.(\d+)\b", section.group())
    if not match:
        raise ValueError("custom_version must use major.feature.build format")
    span = (section.start() + match.start(2), section.start() + match.end(4))
    return span, tuple(map(int, match.group(2, 3, 4)))


def record_version(target, source, env):
    config_path = Path(env.subst("$PROJECT_CONFIG"))
    config = config_path.read_text()
    (start, end), _ = find_version(config, env["PIOENV"])
    config_path.write_text(config[:start] + env["FIRMWARE_VERSION"] + config[end:])
    # PlatformIO deletes the build directory once platformio.ini no longer matches the checksum it
    # stored there. Store the new one, so the image just built survives the next PlatformIO command.
    try:
        from platformio.project.config import ProjectConfig
        from platformio.project.helpers import compute_project_checksum
    except ImportError:
        return
    checksum_path = Path(env.subst("$PROJECT_BUILD_DIR")) / "project.checksum"
    checksum_path.write_text(compute_project_checksum(ProjectConfig(str(config_path))))


def configure_version(env, targets):
    config_path = Path(env.subst("$PROJECT_CONFIG"))
    _, (major, feature, build) = find_version(config_path.read_text(), env["PIOENV"])
    non_firmware_targets = {
        "clean", "cleanall", "idedata", "__idedata", "envdump", "nobuild",
        "buildfs", "uploadfs", "uploadfsota", "erase", "compiledb", "sizedata",
    }
    if not env.IsCleanTarget() and not env.IsIntegrationDump() and not (set(targets) & non_firmware_targets):
        build += 1
        # merge_firmware.py records the number once the image is built. Changing platformio.ini
        # while compiling makes any PlatformIO command started meanwhile, such as an editor's index
        # rebuild, delete build/.
        env["RECORD_FIRMWARE_VERSION"] = record_version

    version = f"{major}.{feature}.{build}"
    env["FIRMWARE_VERSION"] = version
    env.Append(CPPDEFINES=[
        ("VERSION", version),
        ("STRVERSION", '\\"' + version + '\\"'),
        ("STRBUILDDATE", '\\"' + date.today().strftime("%Y.%m.%d") + '\\"'),
    ])
    print(f"Firmware version: {version}")


if "Import" in globals():
    Import("env")
    from SCons.Script import COMMAND_LINE_TARGETS
    configure_version(env, COMMAND_LINE_TARGETS)
