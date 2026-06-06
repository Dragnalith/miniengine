"""Repository rule that downloads the Android NDK from its official channel
and overlays this project's rich BUILD file exposing native_app_glue, glslc,
per-host compiler filegroups, and per-triplet sysroot and libc++_shared
filegroups as labels.

The archive is `android-ndk-<release>-<os>[-x86_64].zip`, always containing a
single top-level `android-ndk-<release>/` directory (e.g. `android-ndk-r29/`).
`strip_prefix` lands its contents at the repo root so paths line up with the
overlay's expectations (`toolchains/llvm/prebuilt/...`, `sources/android/...`,
`shader-tools/<host>/glslc.exe`).
"""

def _release_tag_from_url(url):
    basename = url.rsplit("/", 1)[-1]
    if not basename.startswith("android-ndk-"):
        fail("//build/rules/android: unexpected NDK URL '{}'".format(url))
    core = basename[len("android-ndk-"):]
    for suf in ("-linux-x86_64.zip", "-linux.zip",
                "-darwin-x86_64.zip", "-macosx.zip", "-darwin.zip",
                "-windows-x86_64.zip", "-windows.zip"):
        if core.endswith(suf):
            return core[:-len(suf)]
    fail("//build/rules/android: cannot parse NDK release from '{}'".format(basename))

def _android_ndk_repo_impl(ctx):
    url = ctx.attr.url
    release = _release_tag_from_url(url)

    ctx.report_progress("Fetching android-ndk-{}".format(release))
    ctx.download_and_extract(
        url = url,
        sha256 = ctx.attr.sha256,
        type = "zip",
        stripPrefix = "android-ndk-{}".format(release),
    )

    ctx.template(
        "BUILD.bazel",
        ctx.attr.build_tpl,
        substitutions = {
            "{min_api}": ctx.attr.min_api,
        },
        executable = False,
    )
    return ctx.repo_metadata(reproducible = True)

android_ndk_repo = repository_rule(
    implementation = _android_ndk_repo_impl,
    attrs = {
        "url": attr.string(mandatory = True),
        "sha256": attr.string(default = ""),
        "min_api": attr.string(mandatory = True, doc = "e.g. '24'"),
        "build_tpl": attr.label(
            default = "//build/rules/android/overlays/ndk:BUILD.root.tpl",
            allow_single_file = True,
        ),
    },
)
