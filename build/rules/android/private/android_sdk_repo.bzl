"""Repository rule that downloads Android SDK components from their official
channels (URLs and SHA-256s resolved from F-Droid's transparency log by the
module extension) and overlays this project's rich BUILD file on top.

Layout produced at the repo root (one ANDROID_HOME for everything):

    build-tools/<revision>/...
    platform-tools/...
    platforms/android-<api>/...
    cmdline-tools/<version>/...
    cmdline-tools/latest/                       (copy of the above)
    emulator/                                   (when configured)
    system-images/<api>/<tag>/<abi>/...         (when configured)
    licenses/<license-id>
"""

load(":extract.bzl", "copy_tree", "extract_flattened")
load(":licenses.bzl", "KNOWN_LICENSES")

def _write_licenses(ctx):
    for license_id, hashes in KNOWN_LICENSES.items():
        ctx.file("licenses/{}".format(license_id), hashes)

def _source_properties(ctx, dst):
    props = {}
    for line in ctx.read("{}/source.properties".format(dst)).splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            props[key.strip()] = value.strip()
    return props

def _revision_xml(revision):
    parts = revision.split(".")
    names = ["major", "minor", "micro", "preview"]
    out = []
    for i, value in enumerate(parts[:4]):
        out.append("<{name}>{value}</{name}>".format(
            name = names[i],
            value = value,
        ))
    return "".join(out)

def _xml_escape(value):
    return (value
            .replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace("\"", "&quot;"))

def _write_emulator_package_xml(ctx, dst):
    # The emulator archive ships only source.properties. Modern sdkmanager /
    # avdmanager do not recognize that legacy metadata for the emulator package,
    # so materialize the package.xml they expect in an installed SDK.
    props = _source_properties(ctx, dst)
    revision = props.get("Pkg.Revision", "0")
    display_name = props.get("Pkg.Desc", "Android Emulator")
    ctx.file("{}/package.xml".format(dst), """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<ns2:repository
    xmlns:ns2="http://schemas.android.com/repository/android/common/02"
    xmlns:ns5="http://schemas.android.com/repository/android/generic/02">
  <license id="android-sdk-license" type="text"/>
  <localPackage path="emulator" obsolete="false">
    <type-details xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="ns5:genericDetailsType"/>
    <revision>{revision}</revision>
    <display-name>{display_name}</display-name>
    <uses-license ref="android-sdk-license"/>
  </localPackage>
</ns2:repository>
""".format(
        revision = _revision_xml(revision),
        display_name = _xml_escape(display_name),
    ))

def _android_sdk_repo_impl(ctx):
    components = json.decode(ctx.attr.components)

    for i, comp in enumerate(components):
        extract_flattened(
            ctx,
            url = comp["url"],
            sha256 = comp["sha256"],
            dst = comp["dst"],
            tmp_name = "_tmp_{}".format(i),
        )
        if comp.get("alias_latest"):
            copy_tree(ctx, comp["dst"], "cmdline-tools/latest")
        if comp.get("synthesize_emulator_package_xml"):
            _write_emulator_package_xml(ctx, comp["dst"])

    if ctx.attr.accept_licenses:
        _write_licenses(ctx)

    ctx.template(
        "BUILD.bazel",
        ctx.attr.build_tpl,
        substitutions = {
            "{build_tools}": ctx.attr.build_tools,
            "{platform_version}": ctx.attr.platform_version,
            "{emulator_exe}": ctx.attr.emulator_exe,
        },
        executable = False,
    )
    return ctx.repo_metadata(reproducible = True)

android_sdk_repo = repository_rule(
    implementation = _android_sdk_repo_impl,
    attrs = {
        "components": attr.string(
            mandatory = True,
            doc = "JSON list of {url, sha256, dst[, alias_latest, synthesize_emulator_package_xml]} dicts.",
        ),
        "accept_licenses": attr.bool(default = True),
        "build_tools": attr.string(mandatory = True, doc = "e.g. '36.1.0'"),
        "platform_version": attr.string(mandatory = True, doc = "e.g. '36'"),
        "emulator_exe": attr.string(
            default = "emulator",
            doc = "Basename of the emulator executable inside emulator/ ('emulator' or 'emulator.exe').",
        ),
        "build_tpl": attr.label(
            default = "//build/rules/android/overlays/sdk:BUILD.root.tpl",
            allow_single_file = True,
        ),
    },
)
