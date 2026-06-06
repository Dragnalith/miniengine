"""`adb` wrapper rule.

Materialises an executable target that proxies through to `adb.exe` from
@android_sdk's platform-tools, with its sibling DLLs staged in runfiles so
users can run `bazel run //build/rules/android:adb -- <adb args>` without a system
SDK on PATH.
"""

load("//build/rules/android/private:runner_common.bzl", "rlocation_path", "write_python_launcher")

_PYTHON_TOOLCHAIN = "@rules_python//python:toolchain_type"

def _py_string(value):
    return repr(value)

def _adb_impl(ctx):
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
        fail("adb wrapper currently requires a hermetic Python toolchain.")

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

adb = rule(
    implementation = _adb_impl,
    attrs = {
        "_runner_template": attr.label(
            allow_single_file = True,
            default = "//build/rules/android/private:adb_runner.py.tpl",
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
