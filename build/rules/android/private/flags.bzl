"""Default compile and link flags for the NDK clang/lld toolchain.

These are consumed by `android_ndk_toolchains_repo` when generating the per-group
`flags.bzl` inside the toolchain repo, mirroring the pattern used by
`toolchains_msvc/private/flags.bzl`.
"""

# Base flags applied to every NDK compile action regardless of mode.
NDK_DEFAULT_C_COMPILE_FLAGS = [
    "-fPIC",
    "-funwind-tables",
    "-fstack-protector-strong",
    "-no-canonical-prefixes",
    "-Wformat",
    "-Werror=format-security",
]

NDK_DEFAULT_CXX_COMPILE_FLAGS = NDK_DEFAULT_C_COMPILE_FLAGS + [
    "-std=c++20",
    "-stdlib=libc++",
]

# Base link flags applied to every NDK link action.
NDK_DEFAULT_LINK_FLAGS = [
    "-Wl,--build-id=sha1",
    "-Wl,--no-rosegment",
    "-Wl,--fatal-warnings",
    "-Wl,--no-undefined",
    "-Wl,-z,noexecstack",
    "-Wl,-z,relro",
    "-Wl,-z,now",
    "-stdlib=libc++",
]

# Build-mode flags, keyed by Bazel compilation mode.
NDK_DBG_C_COMPILE_FLAGS = ["-O0", "-g", "-fno-omit-frame-pointer"]
NDK_DBG_CXX_COMPILE_FLAGS = NDK_DBG_C_COMPILE_FLAGS
NDK_DBG_LINK_FLAGS = []

NDK_FASTBUILD_C_COMPILE_FLAGS = ["-O1", "-g"]
NDK_FASTBUILD_CXX_COMPILE_FLAGS = NDK_FASTBUILD_C_COMPILE_FLAGS
NDK_FASTBUILD_LINK_FLAGS = []

NDK_OPT_C_COMPILE_FLAGS = ["-O2", "-DNDEBUG", "-ffunction-sections", "-fdata-sections"]
NDK_OPT_CXX_COMPILE_FLAGS = NDK_OPT_C_COMPILE_FLAGS
NDK_OPT_LINK_FLAGS = ["-Wl,--gc-sections"]
