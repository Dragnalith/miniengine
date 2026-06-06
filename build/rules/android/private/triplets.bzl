"""Android ABI ↔ triplet ↔ Bazel CPU constraint mapping.

The NDK ships four usable Android target triplets (RISC-V omitted). Each one maps
to a single Android ABI string (the `lib/<abi>/` directory inside an APK) and to
a single `@platforms//cpu:*` constraint. The table is the authoritative source of
truth for repo rules and for the android_apk split transition.
"""

TRIPLETS = [
    struct(
        triplet = "aarch64-linux-android",
        abi = "arm64-v8a",
        cpu_constraint = "@platforms//cpu:arm64",
    ),
    struct(
        triplet = "armv7a-linux-androideabi",
        abi = "armeabi-v7a",
        cpu_constraint = "@platforms//cpu:armv7",
    ),
    struct(
        triplet = "x86_64-linux-android",
        abi = "x86_64",
        cpu_constraint = "@platforms//cpu:x86_64",
    ),
    struct(
        triplet = "i686-linux-android",
        abi = "x86",
        cpu_constraint = "@platforms//cpu:x86_32",
    ),
]

TRIPLET_BY_ABI = {t.abi: t for t in TRIPLETS}

# libc++_shared.so lives under <sysroot>/usr/lib/<sysroot_subdir>/ — the armv7a
# triplet uses the legacy "arm-linux-androideabi" subdirectory for its sysroot.
SYSROOT_SUBDIR = {
    "aarch64-linux-android": "aarch64-linux-android",
    "armv7a-linux-androideabi": "arm-linux-androideabi",
    "x86_64-linux-android": "x86_64-linux-android",
    "i686-linux-android": "i686-linux-android",
}

# Supported NDK host prebuilts. Only windows-x86_64 is consumed today; the list
# is where we extend when linux-x86_64 / darwin-* support is added.
HOSTS = [
    struct(
        host = "windows-x86_64",
        os_constraint = "@platforms//os:windows",
        cpu_constraint = "@platforms//cpu:x86_64",
        exe_suffix = ".exe",
    ),
]
