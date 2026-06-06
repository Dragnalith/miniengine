load("@rules_cc//cc/toolchains:args.bzl", "cc_args")
load("@rules_cc//cc/toolchains:feature.bzl", "cc_feature")
load("@rules_cc//cc/toolchains:feature_set.bzl", "cc_feature_set")
load("@rules_cc//cc/toolchains:mutually_exclusive_category.bzl", "cc_mutually_exclusive_category")
load("@rules_cc//cc/toolchains:nested_args.bzl", "cc_nested_args")
load("@rules_cc//cc/toolchains:tool.bzl", "cc_tool")
load("@rules_cc//cc/toolchains:tool_map.bzl", "cc_tool_map")
load("@rules_cc//cc/toolchains:toolchain.bzl", "cc_toolchain")
load(
    ":flags.bzl",
    "default_c_compile_flags",
    "default_cxx_compile_flags",
    "default_link_flags",
    "dbg_c_compile_flags",
    "dbg_cxx_compile_flags",
    "dbg_link_flags",
    "fastbuild_c_compile_flags",
    "fastbuild_cxx_compile_flags",
    "fastbuild_link_flags",
    "opt_c_compile_flags",
    "opt_cxx_compile_flags",
    "opt_link_flags",
)

package(default_visibility = ["//visibility:public"])

# --------------------------------------------------------------------
# Tools
# --------------------------------------------------------------------

cc_tool(
    name = "clang",
    src = "@{ndk_repo}//:clang_{host}",
    data = [
        "@{ndk_repo}//:clang_runtime_{host}",
        "@{ndk_repo}//:sysroot_common_headers",
        "@{ndk_repo}//:sysroot_{triplet}_libs",
    ],
)

cc_tool(
    name = "clangxx",
    src = "@{ndk_repo}//:clangxx_{host}",
    data = [
        "@{ndk_repo}//:clang_runtime_{host}",
        "@{ndk_repo}//:sysroot_common_headers",
        "@{ndk_repo}//:sysroot_{triplet}_libs",
    ],
)

cc_tool(
    name = "ar",
    src = "@{ndk_repo}//:llvm_ar_{host}",
    data = ["@{ndk_repo}//:clang_runtime_{host}"],
)

cc_tool(
    name = "strip",
    src = "@{ndk_repo}//:llvm_strip_{host}",
    data = ["@{ndk_repo}//:clang_runtime_{host}"],
)

cc_tool_map(
    name = "all_tools",
    tools = {
        "@rules_cc//cc/toolchains/actions:c_compile": ":clang",
        "@rules_cc//cc/toolchains/actions:cpp_compile_actions": ":clangxx",
        "@rules_cc//cc/toolchains/actions:assembly_actions": ":clang",
        "@rules_cc//cc/toolchains/actions:link_actions": ":clangxx",
        "@rules_cc//cc/toolchains/actions:ar_actions": ":ar",
        "@rules_cc//cc/toolchains/actions:strip": ":strip",
    },
)

# --------------------------------------------------------------------
# Args
# --------------------------------------------------------------------

# Archive (llvm-ar) plumbing. Without an explicit args-set for the ar action
# group, llvm-ar is invoked with no operation letter and no archive path and
# fails with "archive name must be specified". This mirrors what rules_cc's
# legacy `archiver_flags` feature would emit for a GCC-style toolchain.
cc_args(
    name = "archiver_flags",
    actions = ["@rules_cc//cc/toolchains/actions:ar_actions"],
    args = ["rcsD"],
)

cc_args(
    name = "archiver_output_flags",
    actions = ["@rules_cc//cc/toolchains/actions:ar_actions"],
    args = ["{output_execpath}"],
    format = {"output_execpath": "@rules_cc//cc/toolchains/variables:output_execpath"},
    requires_not_none = "@rules_cc//cc/toolchains/variables:output_execpath",
)

cc_args(
    name = "archiver_input_flags",
    actions = ["@rules_cc//cc/toolchains/actions:ar_actions"],
    nested = [":archiver_libs_iterate"],
    requires_not_none = "@rules_cc//cc/toolchains/variables:libraries_to_link",
)

cc_nested_args(
    name = "archiver_libs_iterate",
    iterate_over = "@rules_cc//cc/toolchains/variables:libraries_to_link",
    nested = [
        ":archiver_link_object_file",
        ":archiver_link_object_file_group",
    ],
)

cc_nested_args(
    name = "archiver_link_object_file",
    args = ["{object_file}"],
    format = {"object_file": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "object_file",
)

cc_nested_args(
    name = "archiver_link_object_file_group",
    args = ["{object_files}"],
    format = {"object_files": "@rules_cc//cc/toolchains/variables:libraries_to_link.object_files"},
    iterate_over = "@rules_cc//cc/toolchains/variables:libraries_to_link.object_files",
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "object_file_group",
)

cc_args(
    name = "default_c_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:c_compile"],
    args = default_c_compile_flags,
)

cc_args(
    name = "default_cxx_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"],
    args = default_cxx_compile_flags,
)

cc_args(
    name = "default_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = default_link_flags,
    data = ["@{ndk_repo}//:sysroot_{triplet}_libs"],
)

# -shared only applies to dynamic-library link actions. Without it clang
# emits a fully-linked executable and crtbegin_dynamic.o demands a main symbol.
cc_args(
    name = "shared_flag_args",
    actions = ["@rules_cc//cc/toolchains/actions:dynamic_library_link_actions"],
    args = ["-shared"],
)

# Pass the final output path via `-o` for every link action — required once a
# rule calls cc_common.link, since rules_cc no longer derives it automatically.
cc_args(
    name = "output_execpath_flags",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = ["-o", "{output_execpath}"],
    format = {"output_execpath": "@rules_cc//cc/toolchains/variables:output_execpath"},
    requires_not_none = "@rules_cc//cc/toolchains/variables:output_execpath",
)

# Forward rule-level `linkopts` (from cc_library / cc_common.link) to the
# linker. Without this, `linkopts` and anything in deps' user_link_flags is
# dropped on the floor — including `-u ANativeActivity_onCreate`.
cc_args(
    name = "user_link_flags_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = ["{user_link_flags}"],
    format = {"user_link_flags": "@rules_cc//cc/toolchains/variables:user_link_flags"},
    iterate_over = "@rules_cc//cc/toolchains/variables:user_link_flags",
    requires_not_none = "@rules_cc//cc/toolchains/variables:user_link_flags",
)

# Emit each library input on the link command. Mirrors the `linker_input` +
# `link_*_library` pattern from rules_cc's legacy linker features: the nested
# args descend through libraries_to_link, expanding static archives, object
# files, whole-archives, and dynamic libraries into their respective flag
# shapes that GCC-style linkers expect.
cc_args(
    name = "linker_input_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    nested = [":linker_libs_iterate"],
    requires_not_none = "@rules_cc//cc/toolchains/variables:libraries_to_link",
)

cc_nested_args(
    name = "linker_libs_iterate",
    iterate_over = "@rules_cc//cc/toolchains/variables:libraries_to_link",
    nested = [
        ":link_object_file_group",
        ":link_object_file",
        ":link_interface_library",
        ":link_static_library",
        ":link_dynamic_library",
    ],
)

cc_nested_args(
    name = "link_object_file_group",
    args = ["{object_files}"],
    format = {"object_files": "@rules_cc//cc/toolchains/variables:libraries_to_link.object_files"},
    iterate_over = "@rules_cc//cc/toolchains/variables:libraries_to_link.object_files",
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "object_file_group",
)

cc_nested_args(
    name = "link_object_file",
    args = ["{object_file}"],
    format = {"object_file": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "object_file",
)

cc_nested_args(
    name = "link_interface_library",
    args = ["{library}"],
    format = {"library": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "interface_library",
)

cc_nested_args(
    name = "link_static_library",
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "static_library",
    nested = [
        ":link_static_library_regular",
        ":link_static_library_whole_archive",
    ],
)

# `-Wl,--whole-archive` forces the linker to pull in every object from an
# archive that's marked `alwayslink = True` on the cc_library. native_app_glue
# relies on this so its `ANativeActivity_onCreate` entry-point is retained.
cc_nested_args(
    name = "link_static_library_regular",
    args = ["{library}"],
    format = {"library": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_false = "@rules_cc//cc/toolchains/variables:libraries_to_link.is_whole_archive",
)

cc_nested_args(
    name = "link_static_library_whole_archive",
    args = ["-Wl,--whole-archive", "{library}", "-Wl,--no-whole-archive"],
    format = {"library": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_true = "@rules_cc//cc/toolchains/variables:libraries_to_link.is_whole_archive",
)

cc_nested_args(
    name = "link_dynamic_library",
    args = ["{library}"],
    format = {"library": "@rules_cc//cc/toolchains/variables:libraries_to_link.name"},
    requires_equal = "@rules_cc//cc/toolchains/variables:libraries_to_link.type",
    requires_equal_value = "dynamic_library",
)

cc_args(
    name = "dbg_c_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:c_compile"],
    args = dbg_c_compile_flags,
)
cc_args(
    name = "dbg_cxx_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"],
    args = dbg_cxx_compile_flags,
)
cc_args(
    name = "dbg_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = dbg_link_flags,
)

cc_args(
    name = "fastbuild_c_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:c_compile"],
    args = fastbuild_c_compile_flags,
)
cc_args(
    name = "fastbuild_cxx_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"],
    args = fastbuild_cxx_compile_flags,
)
cc_args(
    name = "fastbuild_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = fastbuild_link_flags,
)

cc_args(
    name = "opt_c_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:c_compile"],
    args = opt_c_compile_flags,
)
cc_args(
    name = "opt_cxx_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"],
    args = opt_cxx_compile_flags,
)
cc_args(
    name = "opt_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = opt_link_flags,
)

# LTO
cc_args(
    name = "thin_lto_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:compile_actions"],
    args = ["-flto=thin"],
)
cc_args(
    name = "thin_lto_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = ["-flto=thin"],
)
cc_args(
    name = "full_lto_compile_args",
    actions = ["@rules_cc//cc/toolchains/actions:compile_actions"],
    args = ["-flto"],
)
cc_args(
    name = "full_lto_link_args",
    actions = ["@rules_cc//cc/toolchains/actions:link_actions"],
    args = ["-flto"],
)

# C++ standard
cc_args(name = "cxx_standard_14_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++14"])
cc_args(name = "cxx_standard_17_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++17"])
cc_args(name = "cxx_standard_20_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++20"])
cc_args(name = "cxx_standard_23_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++23"])
cc_args(name = "cxx_standard_26_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++26"])
cc_args(name = "cxx_standard_latest_args", actions = ["@rules_cc//cc/toolchains/actions:cpp_compile_actions"], args = ["-std=c++2c"])

# --------------------------------------------------------------------
# Features
# --------------------------------------------------------------------

cc_feature(
    name = "default",
    feature_name = "default",
    args = [
        ":default_c_compile_args",
        ":default_cxx_compile_args",
        ":default_link_args",
        ":shared_flag_args",
        ":output_execpath_flags",
        ":user_link_flags_args",
        ":linker_input_args",
        ":archiver_flags",
        ":archiver_output_flags",
        ":archiver_input_flags",
    ],
)

cc_mutually_exclusive_category(name = "compilation_mode")

cc_feature(
    name = "dbg",
    overrides = "@rules_cc//cc/toolchains/features:dbg",
    mutually_exclusive = [":compilation_mode"],
    args = [":dbg_c_compile_args", ":dbg_cxx_compile_args", ":dbg_link_args"],
)
cc_feature(
    name = "fastbuild",
    overrides = "@rules_cc//cc/toolchains/features:fastbuild",
    mutually_exclusive = [":compilation_mode"],
    args = [":fastbuild_c_compile_args", ":fastbuild_cxx_compile_args", ":fastbuild_link_args"],
)
cc_feature(
    name = "opt",
    overrides = "@rules_cc//cc/toolchains/features:opt",
    mutually_exclusive = [":compilation_mode"],
    args = [":opt_c_compile_args", ":opt_cxx_compile_args", ":opt_link_args"],
)

cc_mutually_exclusive_category(name = "lto")
cc_feature(
    name = "thinlto",
    feature_name = "thinlto",
    mutually_exclusive = [":lto"],
    args = [":thin_lto_compile_args", ":thin_lto_link_args"],
)
cc_feature(
    name = "fulllto",
    feature_name = "fulllto",
    mutually_exclusive = [":lto"],
    args = [":full_lto_compile_args", ":full_lto_link_args"],
)

cc_mutually_exclusive_category(name = "cxx_standard")
cc_feature(name = "cxx_standard_14", feature_name = "cxx_standard_14", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_14_args"])
cc_feature(name = "cxx_standard_17", feature_name = "cxx_standard_17", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_17_args"])
cc_feature(name = "cxx_standard_20", feature_name = "cxx_standard_20", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_20_args"])
cc_feature(name = "cxx_standard_23", feature_name = "cxx_standard_23", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_23_args"])
cc_feature(name = "cxx_standard_26", feature_name = "cxx_standard_26", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_26_args"])
cc_feature(name = "cxx_standard_latest", feature_name = "cxx_standard_latest", mutually_exclusive = [":cxx_standard"], args = [":cxx_standard_latest_args"])

cc_feature_set(
    name = "all_known_features",
    all_of = [
        ":default",
        ":dbg",
        ":fastbuild",
        ":opt",
        ":thinlto",
        ":fulllto",
        ":cxx_standard_14",
        ":cxx_standard_17",
        ":cxx_standard_20",
        ":cxx_standard_23",
        ":cxx_standard_26",
        ":cxx_standard_latest",
    ],
)

# --------------------------------------------------------------------
# cc_toolchain
# --------------------------------------------------------------------

cc_toolchain(
    name = "cc_toolchain",
    compiler = "clang",
    tool_map = ":all_tools",
    enabled_features = [":default"],
    known_features = [":all_known_features"],
)
