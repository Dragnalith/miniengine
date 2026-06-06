"""Bazel module extension for the in-tree Android rules.

Resolves Android SDK component and NDK URLs from Google's
`repository2-3.xml` manifest and materialises `@android_sdk`, `@android_ndk`,
`@android_sdk_toolchains`, and `@android_ndk_toolchains`. The optional
emulator and system-image components are folded into `@android_sdk` itself
so the resulting layout is a single ANDROID_HOME — no per-target view
construction is needed when running `bazel run //path/to:emulator`.

Typical `MODULE.bazel` pattern:

```
android = use_extension("//build/rules/android:extensions.bzl", "android")
android.configure(
    build_tools      = "36.1.0",
    platform_version = "36",
    ndk              = "29.0.14206865",
    min_api          = "24",
)
use_repo(android, "android_sdk", "android_ndk",
         "android_sdk_toolchains", "android_ndk_toolchains")
register_toolchains(
    "@android_sdk_toolchains//:sdk_toolchain",
    "@android_ndk_toolchains//:all",
)
```

Only the root module may declare the `configure` tag.

Environment overrides:
  * `ANDROID_SDK_HOST_OS` — override host OS detection.
"""

load("//build/rules/android/private:android_ndk_repo.bzl", "android_ndk_repo")
load("//build/rules/android/private:android_ndk_toolchains_repo.bzl", "android_ndk_toolchains_repo")
load("//build/rules/android/private:android_sdk_repo.bzl", "android_sdk_repo")
load("//build/rules/android/private:android_sdk_toolchains_repo.bzl", "android_sdk_toolchains_repo")
load("//build/rules/android/private:google_repo.bzl", "build_index", "resolve_version")
load("//build/rules/android/private:host_os.bzl", "detect_host_os")

SDK_REPO_NAME = "android_sdk"
NDK_REPO_NAME = "android_ndk"
SDK_TOOLCHAINS_REPO_NAME = "android_sdk_toolchains"
NDK_TOOLCHAINS_REPO_NAME = "android_ndk_toolchains"

def _sdk_components(index, cfg, host_os):
    """Resolve (url, sha256, dst) records for each requested SDK component."""
    out = []

    rec = resolve_version(index, "build-tools", cfg.build_tools, host_os)
    out.append({
        "url": rec.url,
        "sha256": rec.sha256,
        "dst": "build-tools/{}".format(rec.resolved_version),
    })

    rec = resolve_version(index, "platforms", "android-" + cfg.platform_version, host_os)
    out.append({
        "url": rec.url,
        "sha256": rec.sha256,
        "dst": "platforms/{}".format(rec.resolved_version),
    })

    if cfg.platform_tools:
        rec = resolve_version(index, "platform-tools", cfg.platform_tools, host_os)
        out.append({
            "url": rec.url,
            "sha256": rec.sha256,
            "dst": "platform-tools",
        })

    if cfg.cmdline_tools:
        rec = resolve_version(index, "cmdline-tools", cfg.cmdline_tools, host_os)
        out.append({
            "url": rec.url,
            "sha256": rec.sha256,
            "dst": "cmdline-tools/{}".format(rec.resolved_version),
            # Mirror to `cmdline-tools/latest` so paths match Google's own
            # layout convention, but skip if the resolved version itself is
            # already the "latest" alias.
            "alias_latest": rec.resolved_version != "latest",
        })

    if cfg.emulator:
        rec = resolve_version(index, "emulator", cfg.emulator, host_os)
        out.append({
            "url": rec.url,
            "sha256": rec.sha256,
            "dst": "emulator",
            # The emulator archive ships only legacy source.properties; modern
            # avdmanager looks for package.xml, so synthesise one after extract.
            "synthesize_emulator_package_xml": True,
        })

    if cfg.system_image:
        parts = cfg.system_image.split(";")
        if len(parts) != 3:
            fail(
                "//build/rules/android: system_image must be 'api;tag;abi', got '{}'"
                    .format(cfg.system_image),
            )
        api, tag, abi = parts
        rec = resolve_version(index, "system-images", cfg.system_image, host_os)
        out.append({
            "url": rec.url,
            "sha256": rec.sha256,
            "dst": "system-images/{}/{}/{}".format(api, tag, abi),
        })

    return out

def _emulator_exe(host_os):
    return "emulator.exe" if host_os == "windows" else "emulator"

def _android_impl(mctx):
    cfg = None
    for mod in mctx.modules:
        if not mod.is_root:
            fail(
                "The 'android' module extension may only be used by the root " +
                "module (saw '{}').".format(mod.name),
            )
        for tag in mod.tags.configure:
            if cfg != None:
                fail("android.configure(...) may only be declared once.")
            cfg = tag

    if cfg == None:
        fail("android.configure(...) must be declared exactly once.")

    host_os = detect_host_os(mctx, cfg.host_os)
    index = build_index(mctx)

    components = _sdk_components(index, cfg, host_os)
    android_sdk_repo(
        name = SDK_REPO_NAME,
        components = json.encode(components),
        accept_licenses = cfg.accept_licenses,
        build_tools = cfg.build_tools,
        platform_version = cfg.platform_version,
        emulator_exe = _emulator_exe(host_os),
    )

    ndk_rec = resolve_version(index, "ndk", cfg.ndk, host_os)
    android_ndk_repo(
        name = NDK_REPO_NAME,
        url = ndk_rec.url,
        sha256 = ndk_rec.sha256,
        min_api = cfg.min_api,
    )

    android_sdk_toolchains_repo(
        name = SDK_TOOLCHAINS_REPO_NAME,
        sdk_repo_name = SDK_REPO_NAME,
    )
    android_ndk_toolchains_repo(
        name = NDK_TOOLCHAINS_REPO_NAME,
        ndk_repo_name = NDK_REPO_NAME,
        min_api = cfg.min_api,
    )

    return mctx.extension_metadata(reproducible = True)

_configure_tag = tag_class(attrs = {
    "build_tools": attr.string(mandatory = True, doc = "Pkg.Revision of build-tools, e.g. '36.1.0'."),
    "platform_version": attr.string(mandatory = True, doc = "Numeric API level, e.g. '36'. Used to look up 'android-<N>' in platforms."),
    "ndk": attr.string(mandatory = True, doc = "NDK release tag (e.g. 'r29') or dotted revision (e.g. '29.0.14206865')."),
    "min_api": attr.string(mandatory = True, doc = "Minimum Android API level the NDK toolchain targets, e.g. '24'."),
    "platform_tools": attr.string(default = "", doc = "Optional platform-tools version (e.g. '34.0.3' or 'latest'); empty to skip."),
    "cmdline_tools": attr.string(default = "", doc = "Optional cmdline-tools version (e.g. '11.0' or 'latest'); empty to skip."),
    "emulator": attr.string(default = "", doc = "Optional emulator package version (e.g. 'latest'); empty to skip. Unpacked under @android_sdk//:emulator/."),
    "system_image": attr.string(default = "", doc = "Optional system-image package ('<api>;<tag>;<abi>', e.g. 'android-36;google_apis;x86_64'); empty to skip. Unpacked under @android_sdk//:system-images/<api>/<tag>/<abi>/."),
    "accept_licenses": attr.bool(default = True, doc = "Write android-sdk-license hashes into @android_sdk/licenses/."),
    "host_os": attr.string(default = "", doc = "Override host OS ('linux' | 'macosx' | 'windows'); empty auto-detects."),
})

android = module_extension(
    implementation = _android_impl,
    tag_classes = {
        "configure": _configure_tag,
    },
    environ = ["ANDROID_SDK_HOST_OS"],
)
