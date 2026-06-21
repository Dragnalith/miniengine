"""Generated runner for //tools:control_target.

Drives a connected Android target through `adb`:

    bazel run //tools:control_target -- screenshot out.png
    bazel run //tools:control_target -- input "TAP 40,570; WAIT 200; TAP 100,150; SCREENSHOT out.png"
    bazel run //tools:control_target -- input --file gestures.txt

The `input` stream is a list of `;`-separated commands:

    TAP x,y               tap at pixel (x, y)
    SWIPE x1,y1,x2,y2[,ms] swipe, optional duration in ms (default 300)
    WAIT ms               sleep for ms milliseconds
    TEXT some text        type text (spaces preserved)
    KEY KEYCODE_BACK      send a key event
    SCREENSHOT path       capture a screenshot to path

Relative output paths are resolved against the directory `bazel run` was
launched from (BUILD_WORKING_DIRECTORY), so they land where the user expects.
"""

import argparse
import os
import subprocess
import sys
import time

MAIN_REPOSITORY = __MAIN_REPOSITORY__
ADB_RLOCATION = __ADB_RLOCATION__

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


def resolve_out_path(path):
    # `bazel run` executes in the runfiles tree, not the user's shell cwd, so
    # resolve relative output paths against the launch directory instead.
    if os.path.isabs(path):
        return path
    base = os.environ.get("BUILD_WORKING_DIRECTORY") or os.getcwd()
    return os.path.normpath(os.path.join(base, path))


def adb_base(adb, serial):
    return [adb, "-s", serial] if serial else [adb]


def run_capture(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def run_checked(cmd):
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        if result.stdout:
            sys.stdout.write(result.stdout)
        pretty = " ".join(('"%s"' % c if " " in c else c) for c in cmd)
        raise SystemExit("command failed (%d): %s" % (result.returncode, pretty))
    return result


def ready_devices(adb):
    subprocess.run([adb, "start-server"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = run_capture([adb, "devices"])
    rows = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            rows.append(parts[0])
    return rows


def select_serial(adb, wanted):
    devices = ready_devices(adb)
    if wanted:
        if wanted in devices:
            return wanted
        raise SystemExit(
            "Serial '%s' is not ready. Ready: %s" % (wanted, ", ".join(devices) or "none")
        )
    # Prefer a physical device over an emulator, mirroring the apk runner.
    physical = [s for s in devices if not s.startswith("emulator-")]
    if physical:
        return physical[0]
    if devices:
        return devices[0]
    raise SystemExit("No ready device or emulator. Connect one or start the emulator.")


def take_screenshot(adb, serial, path):
    out = resolve_out_path(path)
    parent = os.path.dirname(out)
    if parent:
        os.makedirs(parent, exist_ok=True)
    # exec-out streams raw bytes (no PTY newline translation), so the PNG is
    # byte-exact. Opening in "wb" truncates, which overrides any existing file.
    result = subprocess.run(
        adb_base(adb, serial) + ["exec-out", "screencap", "-p"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise SystemExit("screencap failed: %s" % result.stderr.decode("utf-8", "replace").strip())
    with open(out, "wb") as f:
        f.write(result.stdout)
    print("screenshot: %s" % out)


def _two_ints(rest, verb):
    parts = [p.strip() for p in rest.split(",")]
    if len(parts) != 2 or not all(p.lstrip("-").isdigit() for p in parts):
        raise SystemExit("%s expects 'x,y', got '%s'" % (verb, rest))
    return parts[0], parts[1]


def do_input_command(adb, serial, verb, rest):
    base = adb_base(adb, serial)
    if verb == "TAP":
        x, y = _two_ints(rest, "TAP")
        run_checked(base + ["shell", "input", "tap", x, y])
    elif verb == "SWIPE":
        parts = [p.strip() for p in rest.split(",")]
        if len(parts) not in (4, 5) or not all(p.lstrip("-").isdigit() for p in parts):
            raise SystemExit("SWIPE expects 'x1,y1,x2,y2[,ms]', got '%s'" % rest)
        duration = parts[4] if len(parts) == 5 else "300"
        run_checked(base + ["shell", "input", "swipe"] + parts[:4] + [duration])
    elif verb == "WAIT":
        if not rest.strip().isdigit():
            raise SystemExit("WAIT expects milliseconds, got '%s'" % rest)
        time.sleep(int(rest.strip()) / 1000.0)
    elif verb == "TEXT":
        # adb input text uses %s for spaces; pass the rest verbatim otherwise.
        run_checked(base + ["shell", "input", "text", rest.replace(" ", "%s")])
    elif verb == "KEY":
        if not rest.strip():
            raise SystemExit("KEY expects a keycode (e.g. KEYCODE_BACK)")
        run_checked(base + ["shell", "input", "keyevent", rest.strip()])
    elif verb == "SCREENSHOT":
        if not rest.strip():
            raise SystemExit("SCREENSHOT expects a path")
        take_screenshot(adb, serial, rest.strip())
    else:
        raise SystemExit("Unknown input command '%s'" % verb)


def run_input_stream(adb, serial, stream):
    for raw in stream.split(";"):
        segment = raw.strip()
        if not segment:
            continue
        verb, _, rest = segment.partition(" ")
        print(">> %s" % segment)
        do_input_command(adb, serial, verb.upper(), rest.strip())


def main():
    parser = argparse.ArgumentParser(
        prog="bazel run //tools:control_target --",
        description="Screenshot and input automation for a connected Android target.",
    )
    parser.add_argument(
        "-s",
        "--serial",
        default=None,
        help="adb serial to target. Defaults to the connected device (physical preferred).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sp_shot = sub.add_parser("screenshot", help="Capture a screenshot to <path>.")
    sp_shot.add_argument("path", help="Output file path (overwritten if it exists).")

    sp_input = sub.add_parser("input", help="Run an input/gesture stream.")
    sp_input.add_argument("stream", nargs="?", default=None, help="Inline ';'-separated command stream.")
    sp_input.add_argument("--file", default=None, help="Read the command stream from a file.")

    opts = parser.parse_args()

    adb = rlocation(ADB_RLOCATION)
    serial = select_serial(adb, opts.serial)

    if opts.command == "screenshot":
        take_screenshot(adb, serial, opts.path)
        return

    if opts.stream is not None and opts.file is not None:
        raise SystemExit("Pass either an inline stream or --file, not both.")
    if opts.file is not None:
        stream_path = resolve_out_path(opts.file)
        try:
            with open(stream_path, "r", encoding="utf-8") as f:
                stream = f.read()
        except OSError as exc:
            raise SystemExit("Cannot read --file '%s': %s" % (stream_path, exc))
    elif opts.stream is not None:
        stream = opts.stream
    else:
        raise SystemExit("input requires an inline stream or --file.")

    run_input_stream(adb, serial, stream)


if __name__ == "__main__":
    main()
