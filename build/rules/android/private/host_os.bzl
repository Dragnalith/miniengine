"""Map Bazel's `ctx.os.name` to the OS tag Google uses in archive filenames.

`build-tools`, `platform-tools`, `ndk`, and `commandlinetools` archives are
published as `<name>-<os>.zip` where `<os>` is one of `linux`, `macosx`,
`windows`. `platform-<api>_rNN.zip` is OS-independent.
"""

_VALID = ("linux", "macosx", "windows")

def detect_host_os(ctx, override = ""):
    env_override = ctx.os.environ.get("ANDROID_SDK_HOST_OS", "")
    if override:
        pick = override
    elif env_override:
        pick = env_override
    else:
        name = ctx.os.name.lower()
        if "windows" in name:
            pick = "windows"
        elif "mac os" in name or "darwin" in name or "osx" in name:
            pick = "macosx"
        elif "linux" in name:
            pick = "linux"
        else:
            fail("//build/rules/android: unsupported host OS '{}'".format(ctx.os.name))

    if pick not in _VALID:
        fail("//build/rules/android: host_os must be one of {}, got '{}'".format(_VALID, pick))
    return pick
