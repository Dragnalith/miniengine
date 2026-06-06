"""`android_emulator` rule implementation."""

load("//build/rules/android/private:runner_common.bzl", "rlocation_path", "write_python_launcher")

_JAVA_RUNTIME_TOOLCHAIN = "@bazel_tools//tools/jdk:runtime_toolchain_type"
_PYTHON_TOOLCHAIN = "@rules_python//python:toolchain_type"

# avdmanager rejects AVD names outside this character set, so reject early at
# analysis time rather than hand the user a cryptic Java stack trace at run.
_AVD_NAME_ALLOWED = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"

def _validate_avd_name(name):
    for ch in name.elems():
        if ch not in _AVD_NAME_ALLOWED:
            fail(
                "android_emulator: target name '{}' contains '{}', which is not valid in an AVD name. Allowed: A-Z a-z 0-9 . _ -".format(name, ch),
            )

def _find_in_runtime(java_runtime, basename):
    for f in java_runtime.files.to_list():
        if f.basename == basename:
            return f
    fail("Could not find {} in Java runtime {}".format(basename, java_runtime))

def _py_string(value):
    return repr(value)

def _android_emulator_impl(ctx):
    _validate_avd_name(ctx.label.name)
    java_runtime = ctx.toolchains[_JAVA_RUNTIME_TOOLCHAIN].java_runtime
    java_exe = _find_in_runtime(java_runtime, "java.exe")

    runner_script = ctx.actions.declare_file(ctx.label.name + "_runner.py")
    ctx.actions.expand_template(
        template = ctx.file._emulator_runner_template,
        output = runner_script,
        substitutions = {
            "__MAIN_REPOSITORY__": _py_string(ctx.workspace_name),
            "__EMULATOR_EXE_RLOCATION__": _py_string(rlocation_path(ctx.file._emulator_exe)),
            "__ADB_RLOCATION__": _py_string(rlocation_path(ctx.file._adb)),
            "__AVDMANAGER_RLOCATION__": _py_string(rlocation_path(ctx.file._avdmanager)),
            "__JAVA_RLOCATION__": _py_string(rlocation_path(java_exe)),
            "__SYSTEM_IMAGE__": _py_string(ctx.attr.system_image),
            "__AVD_NAME__": _py_string(ctx.label.name),
            "__GPU_MODE__": _py_string(ctx.attr.gpu),
        },
        is_executable = True,
    )

    py_runtime = ctx.toolchains[_PYTHON_TOOLCHAIN].py3_runtime
    python_executable = py_runtime.interpreter
    if not python_executable:
        fail("android_emulator currently requires a hermetic Python toolchain.")

    launcher = write_python_launcher(ctx, ctx.label.name, python_executable, runner_script)
    runtime_files = (
        ctx.files._emulator_runtime +
        ctx.files._platform_tools_runtime +
        ctx.files._cmdline_tools_runtime +
        ctx.files._system_image_runtime +
        java_runtime.files.to_list()
    )
    runfiles = ctx.runfiles(
        files = [
            launcher,
            runner_script,
            ctx.file._emulator_exe,
            ctx.file._adb,
            ctx.file._avdmanager,
            java_exe,
        ],
        transitive_files = depset(
            runtime_files,
            transitive = [py_runtime.files],
        ),
    )
    return [DefaultInfo(
        files = depset([launcher, runner_script]),
        executable = launcher,
        runfiles = runfiles,
    )]

android_emulator = rule(
    implementation = _android_emulator_impl,
    attrs = {
        "system_image": attr.string(
            default = "system-images;android-36;google_apis;x86_64",
            doc = "SDK system image package spec used to create the AVD.",
        ),
        "gpu": attr.string(
            default = "swiftshader_indirect",
            doc = "Emulator -gpu mode. The default software renderer suits " +
                  "non-graphical apps; graphical apps that need a capable " +
                  "Vulkan device (e.g. bindless rendering) should use \"host\".",
        ),
        "_emulator_runner_template": attr.label(
            allow_single_file = True,
            default = "//build/rules/android/private:emulator_runner.py.tpl",
        ),
        "_emulator_exe": attr.label(
            allow_single_file = True,
            default = "@android_sdk//:emulator",
        ),
        "_emulator_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:emulator_runtime",
        ),
        "_adb": attr.label(
            allow_single_file = True,
            default = "@android_sdk//:adb",
        ),
        "_platform_tools_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:platform_tools_runtime",
        ),
        "_avdmanager": attr.label(
            allow_single_file = True,
            default = "@android_sdk//:avdmanager",
        ),
        "_cmdline_tools_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:cmdline_tools_runtime",
        ),
        "_system_image_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:system_image_runtime",
        ),
        "_windows_constraint": attr.label(
            default = "@platforms//os:windows",
        ),
    },
    toolchains = [
        _JAVA_RUNTIME_TOOLCHAIN,
        _PYTHON_TOOLCHAIN,
    ],
    executable = True,
)
