"""Windows game bundle rule."""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("//build/rules:providers.bzl", "AssetSpecInfo", "join_relative_path", "validate_relative_dir", "validate_relative_path")

def _is_windows(ctx):
    return ctx.configuration.host_path_separator == ";"

def _endswith_ignore_case(value, suffix):
    return value.lower().endswith(suffix.lower())

def _binary_name_without_exe(exe):
    if not _endswith_ignore_case(exe.basename, ".exe"):
        fail("windows_app binary executable must end with .exe: %s" % exe.short_path)
    return exe.basename[:-4]

def _copy_file(ctx, src, out):
    ctx.actions.symlink(
        output = out,
        target_file = src,
        progress_message = "Bundling %s as %s" % (src.short_path, out.short_path),
    )

def _output_group_files(target, name):
    if OutputGroupInfo not in target:
        return []

    output_groups = target[OutputGroupInfo]
    if not hasattr(output_groups, name):
        return []

    return getattr(output_groups, name).to_list()

def _unique_files(files):
    result = []
    seen = {}
    for file in files:
        if file.path not in seen:
            seen[file.path] = True
            result.append(file)
    return result

def _root_output(ctx, root_basenames, src, basename):
    validate_relative_path(basename, "bundle root output")
    key = basename.lower()
    if key in root_basenames:
        fail("duplicate bundle root output basename: %s" % basename)
    root_basenames[key] = True

    # Keep bundle contents individually declared so the .bat can be returned as
    # DefaultInfo.executable; Bazel cannot execute a child of a tree artifact.
    out = ctx.actions.declare_file(ctx.label.name + "/" + basename)
    _copy_file(ctx, src, out)
    return out

def _asset_output(ctx, used_asset_dests, assets_dir, entry, root_basenames):
    validate_relative_path(entry.dest, "asset dest")
    asset_key = entry.dest.lower()
    if asset_key in used_asset_dests:
        fail("duplicate asset dest: %s" % entry.dest)
    used_asset_dests[asset_key] = True

    if asset_key in root_basenames:
        fail("asset dest collides with bundle root output basename: %s" % entry.dest)

    bundle_dest = join_relative_path(assets_dir, entry.dest)
    validate_relative_path(bundle_dest, "bundle asset path")
    out = ctx.actions.declare_file(ctx.label.name + "/" + bundle_dest)
    _copy_file(ctx, entry.file, out)
    return out

def _windows_app_impl(ctx):
    if not _is_windows(ctx):
        fail("windows_app is Windows-only and must be analyzed on Windows")

    validate_relative_dir(ctx.attr.assets_dir, "assets_dir")

    binary = ctx.executable.binary
    binary_name = _binary_name_without_exe(binary)
    binary_info = ctx.attr.binary[DefaultInfo]
    binary_outputs = binary_info.files.to_list()
    runtime_dynamic_libraries = _output_group_files(ctx.attr.binary, "runtime_dynamic_libraries")
    pdb_files = _output_group_files(ctx.attr.binary, "pdb_file")

    root_basenames = {}
    outputs = []
    outputs.append(_root_output(ctx, root_basenames, binary, binary.basename))

    pdb_name = binary_name + ".pdb"
    for file in _unique_files(binary_outputs + runtime_dynamic_libraries):
        if _endswith_ignore_case(file.basename, ".dll"):
            outputs.append(_root_output(ctx, root_basenames, file, file.basename))

    for file in _unique_files(binary_outputs + pdb_files):
        if file.basename.lower() == pdb_name.lower():
            outputs.append(_root_output(ctx, root_basenames, file, pdb_name))

    bat_name = binary_name + ".bat"
    validate_relative_path(bat_name, "bundle launcher")
    bat_key = bat_name.lower()
    if bat_key in root_basenames:
        fail("duplicate bundle root output basename: %s" % bat_name)
    root_basenames[bat_key] = True

    launcher = ctx.actions.declare_file(ctx.label.name + "/" + bat_name)
    ctx.actions.write(
        output = launcher,
        content = (
            "@echo off\r\n" +
            "cd /d \"%~dp0\"\r\n" +
            "\".\\{exe}\" %*\r\n" +
            "exit /b %ERRORLEVEL%\r\n"
        ).format(exe = binary.basename),
        is_executable = True,
    )
    outputs.append(launcher)

    if ctx.attr.assets:
        used_asset_dests = {}
        for entry in ctx.attr.assets[AssetSpecInfo].entries.to_list():
            outputs.append(_asset_output(ctx, used_asset_dests, ctx.attr.assets_dir, entry, root_basenames))

    return [
        DefaultInfo(
            files = depset(outputs),
            executable = launcher,
            runfiles = ctx.runfiles(files = outputs),
        ),
    ]

windows_app = rule(
    implementation = _windows_app_impl,
    attrs = {
        "assets": attr.label(providers = [AssetSpecInfo]),
        "assets_dir": attr.string(default = "assets"),
        "binary": attr.label(
            executable = True,
            cfg = "target",
            mandatory = True,
            providers = [CcInfo],
        ),
    },
    executable = True,
)
