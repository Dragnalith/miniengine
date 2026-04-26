"""Rule for composing asset specs."""

load("//build/rules:providers.bzl", "AssetSpecInfo", "validate_relative_path")

def _asset_group_impl(ctx):
    entries = []

    for src in ctx.attr.srcs:
        for entry in src[AssetSpecInfo].entries.to_list():
            dest = ctx.attr.prefix + entry.dest
            validate_relative_path(dest, "asset dest from %s" % src.label)
            entries.append(struct(file = entry.file, dest = dest))

    return [
        AssetSpecInfo(entries = depset(entries)),
    ]

asset_group = rule(
    implementation = _asset_group_impl,
    attrs = {
        "prefix": attr.string(default = ""),
        "srcs": attr.label_list(providers = [AssetSpecInfo]),
    },
)
