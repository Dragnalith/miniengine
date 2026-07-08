"""`control_tool` rule.

Generates an executable that drives a connected Android device through `adb`
(screenshots and input/gesture streams). Built on the same hermetic-Python
launcher + runfiles staging as the //build/rules/android runners, so
`bazel run //tools:control_device -- <args>` works with no system SDK on PATH.
"""

load("//build/rules/android/private:runner_common.bzl", "rlocation_path", "write_python_launcher")

_PYTHON_TOOLCHAIN = "@rules_python//python:toolchain_type"

def _py_string(value):
    return repr(value)

def _control_tool_impl(ctx):
    runner_script = ctx.actions.declare_file(ctx.label.name + "_runner.py")
    ctx.actions.expand_template(
        template = ctx.file._runner_template,
        output = runner_script,
        substitutions = {
            "__MAIN_REPOSITORY__": _py_string(ctx.workspace_name),
            "__ADB_RLOCATION__": _py_string(rlocation_path(ctx.file._adb)),
        },
        is_executable = True,
    )

    py_runtime = ctx.toolchains[_PYTHON_TOOLCHAIN].py3_runtime
    python_executable = py_runtime.interpreter
    if not python_executable:
        fail("control_tool currently requires a hermetic Python toolchain.")

    launcher = write_python_launcher(ctx, ctx.label.name, python_executable, runner_script)
    runfiles = ctx.runfiles(
        files = [
            launcher,
            runner_script,
            ctx.file._adb,
        ],
        transitive_files = depset(
            ctx.files._platform_tools_runtime,
            transitive = [py_runtime.files],
        ),
    )
    return [DefaultInfo(
        files = depset([launcher, runner_script]),
        executable = launcher,
        runfiles = runfiles,
    )]

control_tool = rule(
    implementation = _control_tool_impl,
    attrs = {
        "_runner_template": attr.label(
            allow_single_file = True,
            default = "//tools:control_device.py.tpl",
        ),
        "_adb": attr.label(
            allow_single_file = True,
            default = "@android_sdk//:adb",
        ),
        "_platform_tools_runtime": attr.label(
            allow_files = True,
            default = "@android_sdk//:platform_tools_runtime",
        ),
        "_windows_constraint": attr.label(
            default = "@platforms//os:windows",
        ),
    },
    toolchains = [_PYTHON_TOOLCHAIN],
    executable = True,
)
