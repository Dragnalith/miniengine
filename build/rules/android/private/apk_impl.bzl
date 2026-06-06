"""`android_apk` rule implementation.

Produces a fat APK by split-transitioning `deps` over `--android_platforms`:
one `.so` per ABI is linked through the corresponding NDK cc_toolchain, then
stitched into an aapt2-produced base APK together with the matching
`libc++_shared.so`, zipaligned, and signed with a self-generated debug keystore.

Every action goes through `ctx.actions.run` with a concrete executable —
no `ctx.actions.run_shell` anywhere — so the rule has no hidden dependency on
bash/cmd.exe and runs identically on every host.
"""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("//build/rules/android/private:runner_common.bzl", "rlocation_path", "write_python_launcher")

_JAVA_RUNTIME_TOOLCHAIN = "@bazel_tools//tools/jdk:runtime_toolchain_type"
_SDK_TOOLCHAIN = "//build/rules/android:sdk_toolchain_type"
_PYTHON_TOOLCHAIN = "@rules_python//python:toolchain_type"

# Platforms are authored as //build/rules/android/platform:arm64-v8a,
# //build/rules/android/platform:x86_64 etc. The target name is the Android ABI
# string that goes into lib/<abi>/ inside the APK.
def _abi_from_platform_label(label_str):
    return label_str.rsplit(":", 1)[-1]

def _find_in_runtime(java_runtime, basename):
    # keytool / jar / java aren't exposed directly on the java_runtime provider,
    # so locate them by basename inside the JDK's declared files.
    for f in java_runtime.files.to_list():
        if f.basename == basename:
            return f
    fail("Could not find {} in Java runtime {}".format(basename, java_runtime))

def _android_split_transition_impl(settings, _attr):
    platforms = settings["//command_line_option:android_platforms"]
    if not platforms:
        fail("android_apk requires --android_platforms to be set.")
    return {
        str(p): {"//command_line_option:platforms": str(p)}
        for p in platforms
    }

_android_split_transition = transition(
    implementation = _android_split_transition_impl,
    inputs = ["//command_line_option:android_platforms"],
    outputs = ["//command_line_option:platforms"],
)

def _link_shared(ctx, key, abi, deps):
    cc_toolchain = ctx.split_attr._cc_toolchain[key][cc_common.CcToolchainInfo]
    feature_config = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    linking_contexts = [d[CcInfo].linking_context for d in deps if CcInfo in d]
    linking_output = cc_common.link(
        name = ctx.label.name + "_" + abi,
        actions = ctx.actions,
        feature_configuration = feature_config,
        cc_toolchain = cc_toolchain,
        linking_contexts = linking_contexts,
        output_type = "dynamic_library",
        user_link_flags = ctx.attr.linkopts,
    )
    return linking_output.library_to_link.dynamic_library

def _stage_assets(ctx):
    # Symlink each asset into a sibling directory so the parent path can be
    # passed to aapt2's `-A`. Each declared output is an individual file; aapt2
    # then scans the parent directory at build time.
    if not ctx.files.assets:
        return None, []
    staged = []
    for src in ctx.files.assets:
        out = ctx.actions.declare_file(
            "{name}_assets/{base}".format(
                name = ctx.label.name,
                base = src.basename,
            ),
        )
        ctx.actions.symlink(output = out, target_file = src)
        staged.append(out)
    return staged[0].dirname, staged

def _aapt2_link(ctx, sdk, out_apk, asset_dir_path, staged_assets):
    args = ctx.actions.args()
    args.add("link")
    args.add("-I", sdk.android_jar)
    args.add("--manifest", ctx.file.manifest)
    if asset_dir_path:
        args.add("-A", asset_dir_path)
    args.add("--min-sdk-version", ctx.attr.min_sdk_version)
    args.add("--target-sdk-version", ctx.attr.target_sdk_version)
    args.add("-o", out_apk)
    ctx.actions.run(
        executable = sdk.aapt2,
        arguments = [args],
        inputs = [ctx.file.manifest, sdk.android_jar] + staged_assets,
        outputs = [out_apk],
        mnemonic = "Aapt2Link",
        progress_message = "aapt2 link %s" % out_apk.short_path,
    )

def _singlejar_stitch(ctx, base_apk, abi_to_so, abi_to_libcxx, out_apk):
    # singlejar merges `--sources` (preserving entry compression when
    # `--dont_change_compression` is set) and appends `--resources` (new files
    # whose in-archive path is given as src:dst). For API 24+ the resulting
    # APK installs fine with the usual zipalign + apksigner passes downstream.
    args = ctx.actions.args()
    args.add("--output", out_apk)
    args.add("--sources", base_apk)
    args.add("--dont_change_compression")
    args.add("--normalize")
    args.add("--exclude_build_data")

    inputs = [base_apk]

    # Android requires a classes.dex entry even for pure-NativeActivity apps —
    # PackageManager refuses to install otherwise. Stub dex is a tiny empty
    # DEX shipped with the rules.
    stub_dex = ctx.file._stub_dex
    args.add("--resources", "{}:{}".format(stub_dex.path, "classes.dex"))
    inputs.append(stub_dex)

    for abi, so in abi_to_so.items():
        # Strip the per-ABI suffix cc_common.link added: the app expects
        # lib/<abi>/lib<name>.so, not lib/<abi>/lib<name>_<abi>.so.
        zip_path = "lib/{abi}/lib{name}.so".format(abi = abi, name = ctx.label.name)
        args.add("--resources", "{}:{}".format(so.path, zip_path))
        inputs.append(so)
    for abi, libcxx in abi_to_libcxx.items():
        zip_path = "lib/{abi}/libc++_shared.so".format(abi = abi)
        args.add("--resources", "{}:{}".format(libcxx.path, zip_path))
        inputs.append(libcxx)

    ctx.actions.run(
        executable = ctx.executable._singlejar,
        arguments = [args],
        inputs = inputs,
        outputs = [out_apk],
        mnemonic = "AndroidStitchApk",
        progress_message = "stitching APK %s" % out_apk.short_path,
    )

def _zipalign(ctx, sdk, in_apk, out_apk):
    args = ctx.actions.args()
    args.add("-p")
    args.add("-f")
    args.add("4")
    args.add(in_apk)
    args.add(out_apk)
    ctx.actions.run(
        executable = sdk.zipalign,
        arguments = [args],
        inputs = [in_apk],
        outputs = [out_apk],
        mnemonic = "ZipAlign",
    )

def _apksigner_sign(ctx, sdk, java_runtime, keystore, in_apk, out_apk):
    java_exe = _find_in_runtime(java_runtime, "java.exe")
    args = ctx.actions.args()
    args.add("-jar", sdk.apksigner_jar)
    args.add("sign")
    args.add("--ks", keystore)
    args.add("--ks-pass", "pass:android")
    args.add("--key-pass", "pass:android")
    args.add("--ks-key-alias", "androiddebugkey")
    args.add("--min-sdk-version", ctx.attr.min_sdk_version)
    args.add("--out", out_apk)
    args.add(in_apk)
    ctx.actions.run(
        executable = java_exe,
        arguments = [args],
        tools = [java_runtime.files],
        inputs = [in_apk, keystore, sdk.apksigner_jar],
        outputs = [out_apk],
        mnemonic = "ApkSign",
    )

def _py_string(value):
    return repr(value)

def _write_apk_runner(ctx, sdk, final_apk):
    runner_script = ctx.actions.declare_file(ctx.label.name + "_runner.py")
    default_emulator_path = ""
    default_emulator_runfiles = []
    default_emulator_files = []
    if ctx.attr.default_emulator:
        default_info = ctx.attr.default_emulator[DefaultInfo]
        executable = default_info.files_to_run.executable
        if not executable:
            fail("default_emulator must be an executable target.")
        default_emulator_path = rlocation_path(executable)
        default_emulator_files.append(executable)
        default_emulator_runfiles.append(default_info.default_runfiles)

    ctx.actions.expand_template(
        template = ctx.file._apk_runner_template,
        output = runner_script,
        substitutions = {
            "__MAIN_REPOSITORY__": _py_string(ctx.workspace_name),
            "__ADB_RLOCATION__": _py_string(rlocation_path(ctx.file._adb)),
            "__AAPT2_RLOCATION__": _py_string(rlocation_path(sdk.aapt2)),
            "__APK_RLOCATION__": _py_string(rlocation_path(final_apk)),
            "__DEFAULT_EMULATOR_RLOCATION__": _py_string(default_emulator_path),
        },
        is_executable = True,
    )

    py_runtime = ctx.toolchains[_PYTHON_TOOLCHAIN].py3_runtime
    python_executable = py_runtime.interpreter
    if not python_executable:
        fail("android_apk currently requires a hermetic Python toolchain.")

    launcher = write_python_launcher(ctx, ctx.label.name, python_executable, runner_script)
    runfiles = ctx.runfiles(
        files = [
            final_apk,
            runner_script,
            launcher,
            ctx.file._adb,
            sdk.aapt2,
        ] + default_emulator_files,
        transitive_files = depset(
            ctx.files._platform_tools_runtime,
            transitive = [py_runtime.files],
        ),
    )
    runfiles = runfiles.merge_all(default_emulator_runfiles)
    return launcher, runner_script, runfiles

def _android_apk_impl(ctx):
    sdk = ctx.toolchains[_SDK_TOOLCHAIN].sdktoolchaininfo
    java_runtime = ctx.toolchains[_JAVA_RUNTIME_TOOLCHAIN].java_runtime

    abi_to_so = {}
    abi_to_libcxx = {}
    for key, deps in ctx.split_attr.deps.items():
        abi = _abi_from_platform_label(key)
        abi_to_so[abi] = _link_shared(ctx, key, abi, deps)
        libcxx_target = ctx.attr._libcxx_shared_by_abi[abi]
        abi_to_libcxx[abi] = libcxx_target.files.to_list()[0]

    asset_dir_path, staged_assets = _stage_assets(ctx)
    apk_stem = ctx.attr.apk_name or ctx.label.name
    base_apk = ctx.actions.declare_file(apk_stem + ".base.apk")
    _aapt2_link(ctx, sdk, base_apk, asset_dir_path, staged_assets)

    stitched = ctx.actions.declare_file(apk_stem + ".stitched.apk")
    _singlejar_stitch(ctx, base_apk, abi_to_so, abi_to_libcxx, stitched)

    aligned = ctx.actions.declare_file(apk_stem + ".aligned.apk")
    _zipalign(ctx, sdk, stitched, aligned)

    keystore = ctx.file.debug_key
    final_apk = ctx.actions.declare_file(apk_stem + ".apk")
    _apksigner_sign(ctx, sdk, java_runtime, keystore, aligned, final_apk)

    launcher, runner_script, runfiles = _write_apk_runner(ctx, sdk, final_apk)

    return [DefaultInfo(
        files = depset([final_apk, runner_script, launcher]),
        executable = launcher,
        runfiles = runfiles,
    )]

android_apk = rule(
    implementation = _android_apk_impl,
    attrs = {
        "manifest": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
        "custom_package": attr.string(mandatory = True),
        "apk_name": attr.string(default = ""),
        "deps": attr.label_list(
            providers = [CcInfo],
            cfg = _android_split_transition,
        ),
        "assets": attr.label_list(allow_files = True),
        "assets_dir": attr.string(default = ""),
        "default_emulator": attr.label(
            executable = True,
            cfg = "target",
            allow_files = True,
            doc = "Optional android_emulator target to start when no Android target is available.",
        ),
        "linkopts": attr.string_list(default = []),
        "min_sdk_version": attr.string(default = "24"),
        "target_sdk_version": attr.string(default = "36"),
        # Signing keystore. Defaults to a checked-in debug keystore so every
        # build produces the same APK signature — required for `adb install -r`
        # to succeed against a previous install. Override for release builds.
        "debug_key": attr.label(
            allow_single_file = True,
            default = "//build/rules/android/private:debug_key",
        ),
        "_stub_dex": attr.label(
            allow_single_file = True,
            default = "//build/rules/android/private:stub_dex",
        ),
        "_libcxx_shared_by_abi": attr.string_keyed_label_dict(
            default = {
                "arm64-v8a": "@android_ndk//:libcxx_shared_arm64-v8a",
                "armeabi-v7a": "@android_ndk//:libcxx_shared_armeabi-v7a",
                "x86_64": "@android_ndk//:libcxx_shared_x86_64",
                "x86": "@android_ndk//:libcxx_shared_x86",
            },
            allow_files = True,
        ),
        "_cc_toolchain": attr.label(
            default = "@rules_cc//cc:current_cc_toolchain",
            cfg = _android_split_transition,
        ),
        "_singlejar": attr.label(
            default = "@bazel_tools//tools/jdk:singlejar",
            executable = True,
            cfg = "exec",
            allow_files = True,
        ),
        "_adb": attr.label(
            allow_single_file = True,
            default = "@android_sdk//:adb",
        ),
        "_platform_tools_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:platform_tools_runtime",
        ),
        "_apk_runner_template": attr.label(
            allow_single_file = True,
            default = "//build/rules/android/private:apk_runner.py.tpl",
        ),
        "_windows_constraint": attr.label(
            default = "@platforms//os:windows",
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
    toolchains = [
        _SDK_TOOLCHAIN,
        _JAVA_RUNTIME_TOOLCHAIN,
        _PYTHON_TOOLCHAIN,
    ],
    fragments = ["cpp"],
    executable = True,
)
