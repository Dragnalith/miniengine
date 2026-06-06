"""Generated runner for one android_apk target."""

import argparse
import os
import re
import subprocess
import sys
import time

MAIN_REPOSITORY = __MAIN_REPOSITORY__
ADB_RLOCATION = __ADB_RLOCATION__
AAPT2_RLOCATION = __AAPT2_RLOCATION__
APK_RLOCATION = __APK_RLOCATION__
DEFAULT_EMULATOR_RLOCATION = __DEFAULT_EMULATOR_RLOCATION__

sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)

_MANIFEST_CACHE = None


def _candidate_keys(path):
    yield path
    prefixes = []
    if MAIN_REPOSITORY:
        prefixes.append(MAIN_REPOSITORY)
    if "_main" not in prefixes:
        prefixes.append("_main")
    for prefix in prefixes:
        if not path.startswith(prefix + "/"):
            yield prefix + "/" + path


def _manifest_paths():
    seen = set()
    for path in (
        os.environ.get("RUNFILES_MANIFEST_FILE"),
        os.path.abspath(sys.argv[0]) + ".runfiles_manifest",
        os.path.join(os.path.abspath(sys.argv[0]) + ".runfiles", "MANIFEST"),
    ):
        if path and path not in seen:
            seen.add(path)
            yield path


def _load_manifest():
    global _MANIFEST_CACHE
    if _MANIFEST_CACHE is not None:
        return _MANIFEST_CACHE
    _MANIFEST_CACHE = {}
    for manifest in _manifest_paths():
        try:
            with open(manifest, "r", encoding="utf-8") as f:
                for line in f:
                    key, sep, value = line.rstrip("\n").partition(" ")
                    if sep:
                        _MANIFEST_CACHE[key] = value
            return _MANIFEST_CACHE
        except OSError:
            pass
    return _MANIFEST_CACHE


def rlocation(path):
    if os.path.isabs(path):
        return path

    runfiles_dir = os.environ.get("RUNFILES_DIR")
    if runfiles_dir:
        for key in _candidate_keys(path):
            candidate = os.path.join(runfiles_dir, *key.split("/"))
            if os.path.exists(candidate):
                return candidate

    manifest = _load_manifest()
    for key in _candidate_keys(path):
        if key in manifest:
            return manifest[key]

    raise SystemExit("runfiles: cannot resolve '%s'" % path)


def run(cmd, **kwargs):
    if os.name == "nt" and cmd and cmd[0].lower().endswith((".bat", ".cmd")):
        cmd = [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c"] + cmd
    pretty = " ".join(('"%s"' % c if " " in c else c) for c in cmd)
    print(">>", pretty)
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        raise SystemExit("command failed (%d): %s" % (result.returncode, pretty))
    return result


def run_capture(cmd):
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def adb_devices(adb):
    result = run_capture([adb, "devices"])
    if result.returncode != 0:
        return []
    rows = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.strip().split()
        if len(parts) >= 2:
            rows.append((parts[0], parts[1]))
    return rows


def ready_serials(rows, emulator):
    out = []
    for serial, state in rows:
        if state != "device":
            continue
        is_emulator = serial.startswith("emulator-")
        if is_emulator == emulator:
            out.append(serial)
    return sorted(out)


def describe_rows(rows):
    if not rows:
        print("No adb devices.")
        return
    for serial, state in rows:
        kind = "emulator" if serial.startswith("emulator-") else "device"
        print("%-8s %-16s %s" % (kind, serial, state))


def parse_selector(argv):
    command = "run"
    if argv and argv[0] in ("log", "list"):
        command = argv[0]
        argv = argv[1:]

    parser = argparse.ArgumentParser(
        prog="bazel run //<pkg>:<apk> --",
        description="Install and launch one android_apk, or stream device logs.",
    )
    device_flags = ["-d", "--device"]
    if command == "log":
        device_flags.append("-c")
    parser.add_argument(
        *device_flags,
        dest="device",
        nargs="?",
        const="",
        default=None,
        help="Use a physical device. Optionally pass its adb serial.",
    )
    parser.add_argument(
        "-e",
        "--emulator",
        dest="emulator",
        nargs="?",
        const="",
        default=None,
        help="Use an emulator. Optionally pass its adb serial or port.",
    )
    opts = parser.parse_args(argv)
    if opts.device is not None and opts.emulator is not None:
        raise SystemExit("Pass only one of --device/-d or --emulator/-e.")
    if opts.device is not None:
        return command, "device", opts.device
    if opts.emulator is not None:
        return command, "emulator", opts.emulator
    return command, None, None


def match_identifier(candidates, identifier, kind):
    if identifier == "":
        if not candidates:
            return None
        return candidates[0]

    wanted = identifier
    if kind == "emulator" and identifier.isdigit():
        wanted = "emulator-" + identifier
    if wanted in candidates:
        return wanted
    raise SystemExit(
        "%s '%s' is not ready. Ready %ss: %s"
        % (kind, identifier, kind, ", ".join(candidates) or "none")
    )


def start_default_emulator():
    if not DEFAULT_EMULATOR_RLOCATION:
        return
    emulator = rlocation(DEFAULT_EMULATOR_RLOCATION)
    print("No ready Android target found; starting default emulator.")
    run([emulator, "start"])


def select_serial(adb, selector_kind, identifier, allow_start_default):
    run([adb, "start-server"])
    rows = adb_devices(adb)

    if selector_kind == "device":
        serial = match_identifier(ready_serials(rows, emulator=False), identifier, "device")
        if serial:
            return serial
        raise SystemExit("No ready physical device. Use `list` to inspect adb devices.")

    if selector_kind == "emulator":
        serial = match_identifier(ready_serials(rows, emulator=True), identifier, "emulator")
        if serial:
            return serial
        if allow_start_default:
            start_default_emulator()
            rows = adb_devices(adb)
            serial = match_identifier(ready_serials(rows, emulator=True), identifier, "emulator")
            if serial:
                return serial
        raise SystemExit("No ready emulator. Use `list` to inspect adb devices.")

    devices = ready_serials(rows, emulator=False)
    if devices:
        return devices[0]

    emulators = ready_serials(rows, emulator=True)
    if emulators:
        return emulators[0]

    if allow_start_default:
        start_default_emulator()
        deadline = time.time() + 30
        while time.time() < deadline:
            rows = adb_devices(adb)
            emulators = ready_serials(rows, emulator=True)
            if emulators:
                return emulators[0]
            time.sleep(1)

    raise SystemExit("No ready device or emulator. Use `list` to inspect adb devices.")


def apk_package_and_activity(aapt2, apk):
    result = run_capture([aapt2, "dump", "badging", apk])
    if result.returncode != 0:
        raise SystemExit("aapt2 dump badging failed: %s" % result.stderr)
    pkg_m = re.search(r"package:\s+name='([^']+)'", result.stdout)
    act_m = re.search(r"launchable-activity:\s+name='([^']+)'", result.stdout)
    if not pkg_m:
        raise SystemExit("Could not find package in APK.")
    activity = act_m.group(1) if act_m else "android.app.NativeActivity"
    return pkg_m.group(1), activity


def install_apk(adb, serial, apk, package):
    cmd = [adb, "-s", serial, "install", "-r", "-t", apk]
    pretty = " ".join(('"%s"' % c if " " in c else c) for c in cmd)
    print(">>", pretty)
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    sys.stdout.write(result.stdout)
    if result.returncode == 0:
        return
    if "INSTALL_FAILED_UPDATE_INCOMPATIBLE" not in result.stdout:
        raise SystemExit("command failed (%d): %s" % (result.returncode, pretty))
    print("Signing-key mismatch; uninstalling old %s and retrying." % package)
    run([adb, "-s", serial, "uninstall", package])
    run(cmd)


def launch_apk(adb, aapt2, apk, serial):
    package, activity = apk_package_and_activity(aapt2, apk)
    print("APK package=%s activity=%s" % (package, activity))
    print("Using Android target: %s" % serial)
    install_apk(adb, serial, apk, package)
    run([adb, "-s", serial, "shell", "am", "start", "-n", package + "/" + activity])
    print("Launched %s on %s" % (package, serial))


def stream_log(adb, serial):
    print("Streaming logcat from %s. Press Ctrl-C to stop." % serial)
    try:
        subprocess.run([adb, "-s", serial, "logcat"])
    except KeyboardInterrupt:
        pass


def main():
    adb = rlocation(ADB_RLOCATION)
    aapt2 = rlocation(AAPT2_RLOCATION)
    apk = rlocation(APK_RLOCATION)

    command, selector_kind, identifier = parse_selector(sys.argv[1:])
    if command == "list":
        run([adb, "start-server"])
        describe_rows(adb_devices(adb))
        return

    serial = select_serial(
        adb,
        selector_kind,
        identifier,
        allow_start_default=True,
    )
    if command == "log":
        stream_log(adb, serial)
    else:
        launch_apk(adb, aapt2, apk, serial)


if __name__ == "__main__":
    main()
