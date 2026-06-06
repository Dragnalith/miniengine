"""Generates the @android_ndk_toolchains repo.

Emits one rule-based rules_cc `cc_toolchain` per (host × triplet) pair plus the
matching `toolchain()` declaration at the repo root (so that
`register_toolchains("@android_ndk_toolchains//:all")` picks up every toolchain).

Each (host, triplet) subdirectory gets its own generated `flags.bzl` (with the
triplet-specific `--target=<triplet><min_api>` baked in) and its own BUILD.bazel
expanded from the shared template.
"""

load(":triplets.bzl", "HOSTS", "TRIPLETS")
load(
    ":flags.bzl",
    "NDK_DBG_C_COMPILE_FLAGS",
    "NDK_DBG_CXX_COMPILE_FLAGS",
    "NDK_DBG_LINK_FLAGS",
    "NDK_DEFAULT_C_COMPILE_FLAGS",
    "NDK_DEFAULT_CXX_COMPILE_FLAGS",
    "NDK_DEFAULT_LINK_FLAGS",
    "NDK_FASTBUILD_C_COMPILE_FLAGS",
    "NDK_FASTBUILD_CXX_COMPILE_FLAGS",
    "NDK_FASTBUILD_LINK_FLAGS",
    "NDK_OPT_C_COMPILE_FLAGS",
    "NDK_OPT_CXX_COMPILE_FLAGS",
    "NDK_OPT_LINK_FLAGS",
)

def _starlark_string_list(values):
    return "[" + ", ".join(["\"" + v.replace("\\", "\\\\").replace("\"", "\\\"") + "\"" for v in values]) + "]"

def _flags_bzl(triplet_str, min_api):
    target_arg = "--target=" + triplet_str + min_api

    # `--target=` is baked into every compile and link invocation for the NDK
    # toolchain; it's the single flag that tells clang which triplet + API level
    # to compile for. We prepend so it can be overridden by later user flags.
    c_default = [target_arg] + NDK_DEFAULT_C_COMPILE_FLAGS
    cxx_default = [target_arg] + NDK_DEFAULT_CXX_COMPILE_FLAGS
    link_default = [target_arg] + NDK_DEFAULT_LINK_FLAGS

    lines = [
        "default_c_compile_flags = " + _starlark_string_list(c_default),
        "default_cxx_compile_flags = " + _starlark_string_list(cxx_default),
        "default_link_flags = " + _starlark_string_list(link_default),
        "dbg_c_compile_flags = " + _starlark_string_list(NDK_DBG_C_COMPILE_FLAGS),
        "dbg_cxx_compile_flags = " + _starlark_string_list(NDK_DBG_CXX_COMPILE_FLAGS),
        "dbg_link_flags = " + _starlark_string_list(NDK_DBG_LINK_FLAGS),
        "fastbuild_c_compile_flags = " + _starlark_string_list(NDK_FASTBUILD_C_COMPILE_FLAGS),
        "fastbuild_cxx_compile_flags = " + _starlark_string_list(NDK_FASTBUILD_CXX_COMPILE_FLAGS),
        "fastbuild_link_flags = " + _starlark_string_list(NDK_FASTBUILD_LINK_FLAGS),
        "opt_c_compile_flags = " + _starlark_string_list(NDK_OPT_C_COMPILE_FLAGS),
        "opt_cxx_compile_flags = " + _starlark_string_list(NDK_OPT_CXX_COMPILE_FLAGS),
        "opt_link_flags = " + _starlark_string_list(NDK_OPT_LINK_FLAGS),
    ]
    return "\n".join(lines) + "\n"

def _toolchain_decl(host, triplet):
    return """
toolchain(
    name = "cc_toolchain_{host}_{triplet}",
    toolchain = "//{host}/{triplet}:cc_toolchain",
    toolchain_type = "@rules_cc//cc:toolchain_type",
    exec_compatible_with = [
        "{host_os}",
        "{host_cpu}",
    ],
    target_compatible_with = [
        "{target_cpu}",
        "@platforms//os:android",
    ],
)
""".format(
        host = host.host,
        triplet = triplet.triplet,
        host_os = host.os_constraint,
        host_cpu = host.cpu_constraint,
        target_cpu = triplet.cpu_constraint,
    )

def _android_ndk_toolchains_repo_impl(ctx):
    root_build = "package(default_visibility = [\"//visibility:public\"])\n"

    for host in HOSTS:
        for triplet in TRIPLETS:
            subdir = "{}/{}".format(host.host, triplet.triplet)
            ctx.file(
                "{}/flags.bzl".format(subdir),
                _flags_bzl(triplet.triplet, ctx.attr.min_api),
            )
            ctx.template(
                "{}/BUILD.bazel".format(subdir),
                ctx.attr.toolchain_tpl,
                substitutions = {
                    "{ndk_repo}": ctx.attr.ndk_repo_name,
                    "{host}": host.host,
                    "{triplet}": triplet.triplet,
                },
                executable = False,
            )
            root_build += _toolchain_decl(host, triplet)

    ctx.file("BUILD.bazel", root_build)
    return ctx.repo_metadata(reproducible = True)

android_ndk_toolchains_repo = repository_rule(
    implementation = _android_ndk_toolchains_repo_impl,
    attrs = {
        "ndk_repo_name": attr.string(mandatory = True),
        "min_api": attr.string(mandatory = True),
        "toolchain_tpl": attr.label(
            default = "//build/rules/android/overlays/ndk_toolchains:BUILD.toolchain.tpl",
            allow_single_file = True,
        ),
    },
)
