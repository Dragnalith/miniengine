"""Generates the @android_sdk_toolchains repo.

The repo emits only the *instance* of the SDK toolchain — an
`android_sdk_toolchain(...)` bound to the downloaded tool files plus the
matching `toolchain()` registration. The toolchain *type* and the rule
itself live in the main module at //build/rules/android, mirroring how a
bazel_dep module would expose its public API.

`@android_sdk` (the files repo) stays separate — this repo only emits the
rule-based toolchain plumbing and references @android_sdk file labels.
"""

# Resolved at .bzl load time against this file's package, so `Label()` returns
# the canonical label that's valid from inside the generated repo as well.
_TOOLCHAIN_TYPE = Label("//build/rules/android:sdk_toolchain_type")
_TOOLCHAIN_RULE_BZL = Label("//build/rules/android:sdk_toolchain.bzl")

def _android_sdk_toolchains_repo_impl(ctx):
    ctx.template(
        "BUILD.bazel",
        ctx.attr.build_tpl,
        substitutions = {
            "{sdk_repo}": ctx.attr.sdk_repo_name,
            "{toolchain_type}": str(_TOOLCHAIN_TYPE),
            "{toolchain_rule_bzl}": str(_TOOLCHAIN_RULE_BZL),
        },
        executable = False,
    )
    return ctx.repo_metadata(reproducible = True)

android_sdk_toolchains_repo = repository_rule(
    implementation = _android_sdk_toolchains_repo_impl,
    attrs = {
        "sdk_repo_name": attr.string(mandatory = True),
        "build_tpl": attr.label(
            default = "//build/rules/android/overlays/sdk_toolchains:BUILD.root.tpl",
            allow_single_file = True,
        ),
    },
)
