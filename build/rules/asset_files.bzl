"""Rule for declaring asset files and their bundle destinations."""

load("//build/rules:providers.bzl", "AssetSpecInfo", "validate_relative_dir", "validate_relative_path")

def _clean_strip_prefix(strip_prefix):
    if "\\" in strip_prefix:
        fail("strip_prefix must use forward slashes: %s" % strip_prefix)
    if strip_prefix.startswith("/"):
        fail("strip_prefix must be relative and must not start with '/': %s" % strip_prefix)
    if strip_prefix == "":
        return strip_prefix

    validate_relative_dir(strip_prefix, "strip_prefix")
    return strip_prefix

def _strip_file_short_path(file, strip_prefix):
    if strip_prefix == "":
        return file.short_path

    short_path = file.short_path
    if short_path == strip_prefix:
        fail("strip_prefix removes the entire path for %s" % short_path)
    if not short_path.startswith(strip_prefix + "/"):
        fail("strip_prefix '%s' does not match input %s" % (strip_prefix, short_path))
    return short_path[len(strip_prefix) + 1:]

def _asset_files_impl(ctx):
    strip_prefix = _clean_strip_prefix(ctx.attr.strip_prefix)
    entries = []
    used_renames = {}

    for file in ctx.files.srcs:
        if file.short_path in ctx.attr.renames:
            dest = ctx.attr.prefix + ctx.attr.renames[file.short_path]
            used_renames[file.short_path] = True
        else:
            dest = ctx.attr.prefix + _strip_file_short_path(file, strip_prefix)

        validate_relative_path(dest, "asset dest for %s" % file.short_path)
        entries.append(struct(file = file, dest = dest))

    unused_renames = []
    for key in ctx.attr.renames:
        if key not in used_renames:
            unused_renames.append(key)
    if unused_renames:
        fail("renames keys must match src File.short_path values; unused keys: %s" % ", ".join(unused_renames))

    return [
        AssetSpecInfo(entries = depset(entries)),
    ]

asset_files = rule(
    implementation = _asset_files_impl,
    attrs = {
        "prefix": attr.string(default = ""),
        "renames": attr.string_dict(default = {}),
        "srcs": attr.label_list(allow_files = True),
        "strip_prefix": attr.string(mandatory = True),
    },
)
