load("{toolchain_rule_bzl}", "android_sdk_toolchain")

package(default_visibility = ["//visibility:public"])

android_sdk_toolchain(
    name = "sdk_tools",
    aapt2 = "@{sdk_repo}//:aapt2",
    zipalign = "@{sdk_repo}//:zipalign",
    apksigner_jar = "@{sdk_repo}//:apksigner_jar",
    android_jar = "@{sdk_repo}//:android_jar",
)

toolchain(
    name = "sdk_toolchain",
    toolchain = ":sdk_tools",
    toolchain_type = "{toolchain_type}",
    exec_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
)
