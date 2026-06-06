"""Generated runner for one android_emulator target."""

import argparse
import os
import re
import subprocess
import sys
import time

MAIN_REPOSITORY = __MAIN_REPOSITORY__
EMULATOR_EXE_RLOCATION = __EMULATOR_EXE_RLOCATION__
ADB_RLOCATION = __ADB_RLOCATION__
AVDMANAGER_RLOCATION = __AVDMANAGER_RLOCATION__
JAVA_RLOCATION = __JAVA_RLOCATION__
SYSTEM_IMAGE = __SYSTEM_IMAGE__
AVD_NAME = __AVD_NAME__

BOOT_TIMEOUT_SEC = 60
SHUTDOWN_TIMEOUT_SEC = 60
EMULATOR_PORT_MIN = 5554
EMULATOR_PORT_MAX = 5680

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


def workspace_root():
    return os.environ.get("BUILD_WORKSPACE_DIRECTORY") or os.getcwd()


def hermetic_env(java_home, sdk_root, avd_home):
    env = os.environ.copy()
    env["ANDROID_HOME"] = sdk_root
    env["ANDROID_SDK_ROOT"] = sdk_root
    env["ANDROID_AVD_HOME"] = avd_home
    env["JAVA_HOME"] = java_home
    env["PATH"] = os.path.join(java_home, "bin") + os.pathsep + env.get("PATH", "")
    return env


def run(cmd, env, **kwargs):
    pretty = " ".join(('"%s"' % c if " " in c else c) for c in cmd)
    print(">>", pretty)
    result = subprocess.run(cmd, env=env, **kwargs)
    if result.returncode != 0:
        raise SystemExit("command failed (%d): %s" % (result.returncode, pretty))
    return result


def run_capture(cmd, env):
    return subprocess.run(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def adb_devices(adb, env):
    result = run_capture([adb, "devices"], env)
    if result.returncode != 0:
        return []
    rows = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.strip().split()
        if len(parts) >= 2:
            rows.append((parts[0], parts[1]))
    return rows


def adb_device_state(adb, env, serial):
    return dict(adb_devices(adb, env)).get(serial)


def running_emulator_serials(adb, env):
    return [
        serial
        for serial, _state in adb_devices(adb, env)
        if serial.startswith("emulator-")
    ]


def stale_emulator_transports(adb, env):
    return [
        (serial, state)
        for serial, state in adb_devices(adb, env)
        if serial.startswith("emulator-") and state != "device"
    ]


def format_transports(rows):
    return ", ".join("%s(%s)" % (serial, state) for serial, state in rows)


def reset_adb_server(adb, env, restart):
    print("Resetting adb server.")
    run([adb, "kill-server"], env)
    if restart:
        run([adb, "start-server"], env)


def reset_stale_emulator_transports(adb, env):
    stale = stale_emulator_transports(adb, env)
    if not stale:
        return
    print("Clearing stale adb emulator transports: %s" % format_transports(stale))
    reset_adb_server(adb, env, restart=True)


def powershell_string(value):
    return "'" + value.replace("'", "''") + "'"


def windows_emulator_processes(cfg):
    if os.name != "nt":
        return []

    emulator_dir = os.path.normpath(os.path.dirname(cfg.emulator_exe))
    ps_script = (
        "$needleAvd = " + powershell_string("-avd " + cfg.avd_name) + "\n"
        "$needleHome = " + powershell_string(os.path.normpath(cfg.avd_home)) + "\n"
        "$needleEmulatorDir = " + powershell_string(emulator_dir) + "\n"
        "$needleEmulatorDirSlash = " + powershell_string(emulator_dir.replace("\\", "/")) + "\n"
        "$names = @('cmd.exe', 'emulator.exe', 'qemu-system-x86_64.exe')\n"
        "Get-CimInstance Win32_Process | Where-Object {\n"
        "    $_.CommandLine -and\n"
        "    ($names -contains $_.Name) -and\n"
        "    (\n"
        "        $_.CommandLine.Contains($needleAvd) -or\n"
        "        $_.CommandLine.Contains($needleHome) -or\n"
        # `adb emu kill` leaves short-lived `emulator -kill ...` helpers on Windows.
        "        (\n"
        "            $_.CommandLine.Contains(' -kill ') -and\n"
        "            ($_.CommandLine.Contains($needleEmulatorDir) -or $_.CommandLine.Contains($needleEmulatorDirSlash))\n"
        "        )\n"
        "    )\n"
        "} | ForEach-Object { \"$($_.ProcessId) $($_.Name)\" }\n"
    )
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            ps_script,
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print("Warning: could not query emulator processes: %s" % result.stderr.strip())
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def wait_for_emulator_process_exit(cfg):
    if os.name != "nt":
        return True, []

    deadline = time.time() + SHUTDOWN_TIMEOUT_SEC
    last_processes = []
    while time.time() < deadline:
        processes = windows_emulator_processes(cfg)
        if not processes:
            return True, last_processes
        last_processes = processes
        time.sleep(1)
    return False, last_processes


def used_emulator_ports(adb, env):
    ports = set()
    for serial in running_emulator_serials(adb, env):
        match = re.match(r"emulator-(\d+)", serial)
        if match:
            ports.add(int(match.group(1)))
    return ports


def pick_free_port(used):
    for port in range(EMULATOR_PORT_MIN, EMULATOR_PORT_MAX + 1, 2):
        if port not in used:
            return port
    raise SystemExit(
        "No free emulator console port in [%d, %d]." % (EMULATOR_PORT_MIN, EMULATOR_PORT_MAX)
    )


def query_avd_name(adb, env, serial):
    """Ask the emulator's console which AVD it's running. Returns None if the
    query fails (e.g. the emulator's console isn't ready yet)."""
    result = run_capture([adb, "-s", serial, "emu", "avd", "name"], env)
    if result.returncode != 0:
        return None
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line or line == "OK":
            continue
        return line
    return None


def find_our_serial(adb, env, avd_name):
    """Return the emulator-NNNN serial running our AVD, or None.

    Live source of truth: poll every running emulator's console for its AVD
    name. No on-disk state file to drift from reality.
    """
    for serial in running_emulator_serials(adb, env):
        if query_avd_name(adb, env, serial) == avd_name:
            return serial
    return None


def avd_exists(avd_home, avd_name):
    return os.path.isfile(os.path.join(avd_home, avd_name + ".ini"))


def repair_avd_pointer(avd_home, avd_name):
    ini_path = os.path.join(avd_home, avd_name + ".ini")
    expected = os.path.join(avd_home, avd_name + ".avd")
    try:
        with open(ini_path, "r", encoding="utf-8") as f:
            lines = f.readlines()
    except OSError:
        return

    changed = False
    out = []
    for line in lines:
        if line.startswith("path="):
            current = line[len("path="):].rstrip("\r\n")
            if os.path.normcase(os.path.normpath(current)) != os.path.normcase(os.path.normpath(expected)):
                print("Repairing stale AVD path: %s -> %s" % (current, expected))
                line = "path=%s\n" % expected
                changed = True
        out.append(line)
    if changed:
        with open(ini_path, "w", encoding="utf-8") as f:
            f.writelines(out)


def create_avd(cfg):
    print("Creating AVD '%s' using %s" % (cfg.avd_name, cfg.system_image))
    os.makedirs(cfg.avd_home, exist_ok=True)

    # ANDROID_HOME already points at @android_sdk, which holds cmdline-tools,
    # platforms, system-images and emulator in their canonical layout — so
    # avdmanager finds everything natively, no view construction needed.
    avd_env = dict(cfg.env)
    avd_env["JAVA_OPTS"] = (
        '-Dcom.android.sdkmanager.toolsdir="%s"'
        % os.path.join(cfg.sdk_root, "cmdline-tools", "latest")
    )

    proc = subprocess.Popen(
        [
            cfg.avdmanager,
            "create",
            "avd",
            "-n",
            cfg.avd_name,
            "-k",
            cfg.system_image,
            "--force",
        ],
        env=avd_env,
        stdin=subprocess.PIPE,
        text=True,
    )
    proc.communicate(input="no\n")
    if proc.returncode != 0:
        raise SystemExit("avdmanager create failed (%d)" % proc.returncode)


def spawn_emulator_detached(cfg, port):
    print("Starting emulator '%s' on port %d (log: %s)" % (cfg.avd_name, port, cfg.log_path))
    cmd = [
        cfg.emulator_exe,
        "-avd",
        cfg.avd_name,
        "-sysdir",
        cfg.sysdir,
        "-port",
        str(port),
        "-no-snapshot-save",
        "-no-audio",
        "-no-boot-anim",
        "-gpu",
        "swiftshader_indirect",
    ]

    if os.name != "nt":
        with open(cfg.log_path, "ab") as log:
            proc = subprocess.Popen(
                cmd,
                env=cfg.env,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
        return proc.pid

    env_setters = " & ".join(
        'set "%s=%s"' % (key, cfg.env[key])
        for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT", "ANDROID_AVD_HOME", "JAVA_HOME")
    )
    emulator_cmd = (
        '"%s" -avd %s -sysdir "%s" -port %d -no-snapshot-save -no-audio '
        '-no-boot-anim -gpu swiftshader_indirect > "%s" 2>&1'
        % (cfg.emulator_exe, cfg.avd_name, cfg.sysdir, port, cfg.log_path)
    )
    target = "cmd.exe /d /c " + env_setters + " & " + emulator_cmd
    ps_script = (
        "$cmd = '" + target.replace("'", "''") + "'\n"
        "$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create "
        "-Arguments @{ CommandLine = $cmd }\n"
        "if ($r.ReturnValue -ne 0) {\n"
        "    Write-Error ('WMI Win32_Process.Create returned ' + $r.ReturnValue)\n"
        "    exit 1\n"
        "}\n"
        "Write-Output $r.ProcessId\n"
    )
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            ps_script,
        ],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(
            "Failed to spawn emulator via WMI (rc=%d)\nstdout: %s\nstderr: %s"
            % (result.returncode, result.stdout.strip(), result.stderr.strip())
        )
    return int(result.stdout.strip())


def wait_for_boot(cfg, serial):
    print("Waiting for %s to boot (up to %ds)..." % (serial, BOOT_TIMEOUT_SEC))
    deadline = time.time() + BOOT_TIMEOUT_SEC
    reset_after_offline = True

    while time.time() < deadline:
        state = adb_device_state(cfg.adb, cfg.env, serial)
        if state == "device":
            break
        if state == "offline" and reset_after_offline:
            print("%s is offline in adb; restarting adb server once." % serial)
            reset_adb_server(cfg.adb, cfg.env, restart=True)
            reset_after_offline = False
        time.sleep(2)
    else:
        raise SystemExit("Emulator %s never reached 'device' state." % serial)

    while time.time() < deadline:
        result = run_capture(
            [cfg.adb, "-s", serial, "shell", "getprop", "sys.boot_completed"],
            cfg.env,
        )
        if result.returncode == 0 and result.stdout.strip() == "1":
            break
        time.sleep(2)
    else:
        raise SystemExit("Timed out waiting for sys.boot_completed=1 on %s." % serial)

    while time.time() < deadline:
        result = run_capture([cfg.adb, "-s", serial, "shell", "pm", "path", "android"], cfg.env)
        if result.returncode == 0:
            print("Emulator %s is booted." % serial)
            return
        time.sleep(2)
    raise SystemExit("Timed out waiting for PackageManager on %s." % serial)


class Config:
    def __init__(self):
        self.avd_name = AVD_NAME
        self.state_dir = os.path.join(
            workspace_root(), ".android_emulators", self.avd_name
        )
        self.avd_home = self.state_dir
        self.log_path = os.path.join(self.state_dir, "emulator.log")

        self.emulator_exe = rlocation(EMULATOR_EXE_RLOCATION)
        self.adb = rlocation(ADB_RLOCATION)
        self.avdmanager = rlocation(AVDMANAGER_RLOCATION)
        java_exe = rlocation(JAVA_RLOCATION)
        self.java_home = os.path.dirname(os.path.dirname(java_exe))

        # adb lives at <sdk>/platform-tools/adb.exe; the system-image lives at
        # <sdk>/system-images/<api>/<tag>/<abi>/. ANDROID_HOME = <sdk>.
        self.sdk_root = os.path.dirname(os.path.dirname(self.adb))
        self.system_image = SYSTEM_IMAGE
        self.sysdir = os.path.join(self.sdk_root, *SYSTEM_IMAGE.split(";"))
        self.env = hermetic_env(self.java_home, self.sdk_root, self.avd_home)


def cmd_start(cfg):
    run([cfg.adb, "start-server"], cfg.env)
    existing = find_our_serial(cfg.adb, cfg.env, cfg.avd_name)
    if existing:
        raise SystemExit(
            "Emulator '%s' already running as %s. Run `stop` first."
            % (cfg.avd_name, existing)
        )
    processes = windows_emulator_processes(cfg)
    if processes:
        raise SystemExit(
            "Emulator '%s' already has running processes: %s. Run `stop` first."
            % (cfg.avd_name, ", ".join(processes))
        )

    reset_adb_server(cfg.adb, cfg.env, restart=True)
    reset_stale_emulator_transports(cfg.adb, cfg.env)
    existing = find_our_serial(cfg.adb, cfg.env, cfg.avd_name)
    if existing:
        raise SystemExit(
            "Emulator '%s' already running as %s. Run `stop` first."
            % (cfg.avd_name, existing)
        )

    os.makedirs(cfg.state_dir, exist_ok=True)

    if avd_exists(cfg.avd_home, cfg.avd_name):
        print("Reusing existing AVD '%s' (%s)" % (cfg.avd_name, cfg.avd_home))
        repair_avd_pointer(cfg.avd_home, cfg.avd_name)
    else:
        create_avd(cfg)

    port = pick_free_port(used_emulator_ports(cfg.adb, cfg.env))
    serial = "emulator-%d" % port
    pid = spawn_emulator_detached(cfg, port)
    print("Emulator spawned (pid=%d, serial=%s)" % (pid, serial))
    wait_for_boot(cfg, serial)
    print("Device ready: %s" % serial)


def wait_for_serial_disconnect(cfg, serial):
    deadline = time.time() + SHUTDOWN_TIMEOUT_SEC
    last_state = None
    while time.time() < deadline:
        state = adb_device_state(cfg.adb, cfg.env, serial)
        if state is None:
            return True, last_state
        last_state = state
        time.sleep(1)
    return False, last_state


def cmd_stop(cfg):
    run([cfg.adb, "start-server"], cfg.env)
    serial = find_our_serial(cfg.adb, cfg.env, cfg.avd_name)
    if not serial:
        reset_stale_emulator_transports(cfg.adb, cfg.env)
        print("Emulator '%s' is not running." % cfg.avd_name)
        return

    print("Stopping %s (%s)" % (cfg.avd_name, serial))
    run([cfg.adb, "-s", serial, "emu", "kill"], cfg.env)
    disconnected, last_state = wait_for_serial_disconnect(cfg, serial)
    if not disconnected:
        print(
            "Emulator '%s' (%s) still listed by adb as %s after %ds."
            % (cfg.avd_name, serial, last_state or "unknown", SHUTDOWN_TIMEOUT_SEC)
        )
    exited, processes = wait_for_emulator_process_exit(cfg)
    if not exited:
        print(
            "Emulator '%s' still has processes after %ds: %s"
            % (cfg.avd_name, SHUTDOWN_TIMEOUT_SEC, ", ".join(processes))
        )
    reset_adb_server(cfg.adb, cfg.env, restart=False)
    print("Emulator stopped.")


def cmd_status(cfg):
    run([cfg.adb, "start-server"], cfg.env)
    serial = find_our_serial(cfg.adb, cfg.env, cfg.avd_name)

    print("avd:      %s" % cfg.avd_name)
    print("running:  %s" % ("yes" if serial else "no"))
    if serial:
        print("serial:   %s" % serial)
    print("state:    %s" % cfg.state_dir)
    if os.path.isfile(cfg.log_path):
        print("log:      %s" % cfg.log_path)


def main():
    parser = argparse.ArgumentParser(
        prog="bazel run //<pkg>:<emulator> --",
        description="Manage one Android emulator target.",
    )
    parser.add_argument("command", choices = ["start", "stop", "status"])
    opts = parser.parse_args()

    cfg = Config()
    if opts.command == "start":
        cmd_start(cfg)
    elif opts.command == "stop":
        cmd_stop(cfg)
    else:
        cmd_status(cfg)


if __name__ == "__main__":
    main()
