load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

# Sources from sources/android/native_app_glue/ consumed by the sample app.
cc_library(
    name = "native_app_glue",
    srcs = ["sources/android/native_app_glue/android_native_app_glue.c"],
    hdrs = ["sources/android/native_app_glue/android_native_app_glue.h"],
    strip_include_prefix = "sources/android/native_app_glue",
)

# SPIR-V shader compiler. Shader-tools are only prebuilt for windows-x86_64 in
# this NDK package.
filegroup(
    name = "glslc",
    srcs = ["shader-tools/windows-x86_64/glslc.exe"],
)

# ------------------------------------------------------------------
# Per-host toolchain filegroups. Only windows-x86_64 today.
# Each `*_all_files_windows-x86_64` rollup is what cc_tool needs as runfiles.
# ------------------------------------------------------------------
filegroup(
    name = "clang_windows-x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"],
)
filegroup(
    name = "clangxx_windows-x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"],
)
filegroup(
    name = "lld_windows-x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/bin/ld.lld.exe"],
)
filegroup(
    name = "llvm_ar_windows-x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-ar.exe"],
)
filegroup(
    name = "llvm_strip_windows-x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe"],
)

# Runtime dependencies of clang.exe on Windows: the bin/ and lib/ trees contain
# clang's own libclang*.dll, libwinpthread-1.dll, and the packaged libclang_rt
# runtimes. The `lib/clang/<ver>/lib/linux/<arch>/libclang_rt.builtins.a` archive
# is needed at link time for soft-floats, compiler-rt builtins, and PAC/BTI stubs
# on aarch64.
filegroup(
    name = "clang_runtime_windows-x86_64",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/bin/*.dll",
        "toolchains/llvm/prebuilt/windows-x86_64/bin/*.exe",
        "toolchains/llvm/prebuilt/windows-x86_64/lib/clang/**",
        "toolchains/llvm/prebuilt/windows-x86_64/lib/libwinpthread-1.dll",
    ], allow_empty = True),
)

# ------------------------------------------------------------------
# Per-triplet sysroot filegroups. Headers live under sysroot/usr/include and
# sysroot/usr/include/<triplet>; libraries live under sysroot/usr/lib/<subdir>.
# The armv7a triplet uses the "arm-linux-androideabi" subdirectory for both
# headers and libraries.
# ------------------------------------------------------------------
filegroup(
    name = "sysroot_common_headers",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/**",
    ]),
)

filegroup(
    name = "sysroot_aarch64-linux-android_libs",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/**",
    ]),
)
filegroup(
    name = "sysroot_armv7a-linux-androideabi_libs",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/arm-linux-androideabi/**",
    ]),
)
filegroup(
    name = "sysroot_x86_64-linux-android_libs",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/x86_64-linux-android/**",
    ]),
)
filegroup(
    name = "sysroot_i686-linux-android_libs",
    srcs = glob([
        "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/i686-linux-android/**",
    ]),
)

# libc++_shared.so copied into lib/<abi>/ of the final APK. NDK 29 keeps a
# single copy per triplet (not per API level) at sysroot/usr/lib/<triplet>/,
# unlike earlier NDKs where each API level had its own.
filegroup(
    name = "libcxx_shared_arm64-v8a",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"],
)
filegroup(
    name = "libcxx_shared_armeabi-v7a",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/arm-linux-androideabi/libc++_shared.so"],
)
filegroup(
    name = "libcxx_shared_x86_64",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/x86_64-linux-android/libc++_shared.so"],
)
filegroup(
    name = "libcxx_shared_x86",
    srcs = ["toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/i686-linux-android/libc++_shared.so"],
)
