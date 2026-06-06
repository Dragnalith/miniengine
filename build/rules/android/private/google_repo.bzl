"""Resolve Android SDK / NDK / system-image archive URLs from Google's
transparency manifests.

The top-level manifest at
`https://dl.google.com/android/repository/repository2-3.xml` publishes every
`<remotePackage>` for build-tools, platforms, platform-tools, cmdline-tools,
NDK, emulator, and more. System-images live in separate per-tag manifests
rooted at `sys-img/<tag>/sys-img2-3.xml`. All of them ship SHA-1 only
(which `ctx.download_and_extract` can't consume), so archive SHA-256s are
hardcoded below. Archive entries without a known SHA-256 are ignored as if
they did not exist.

Archive URLs inside each manifest are relative to the manifest's own
directory, so we carry a base URL alongside each manifest when merging
packages into the index.

`build_index(ctx)` returns:

    {
      "build-tools":    {"36.1.0":      {"linux": rec, "windows": rec, "macosx": rec}, ...},
      "platforms":      {"android-36":  {"all": rec}, ...},
      "platform-tools": {"latest":      {"linux": rec, ...}, ...},
      "cmdline-tools":  {"latest":      {"linux": rec, ...}, ...},
      "ndk":            {"29.0.14206865": {"windows": rec, ...}, ...},
      "emulator":       {"latest":      {"windows": rec}},
      "system-images":  {"android-36;google_apis;x86_64": {"all": rec}, ...},
    }

where `rec` is `struct(url, sha256)`.
"""

REPO_BASE = "https://dl.google.com/android/repository/"
REPO_XML_URL = REPO_BASE + "repository2-3.xml"

# Per-tag system-image manifests. Each XML's archive `<url>` entries are
# relative to the XML's own directory, not REPO_BASE.
_SYS_IMG_TAGS = ("android", "google_apis", "google_apis_playstore")

_CATEGORIES = (
    "build-tools",
    "platforms",
    "platform-tools",
    "cmdline-tools",
    "ndk",
    "emulator",
    "system-images",
)

# Hardcoded allowlist for the Android packages this repository currently uses.
# The key format is:
#
#     <remotePackage path>|<host-os or all>|<absolute archive URL>
#
# Packages missing from this table are treated as absent from Google's
# manifests. This keeps `latest` from silently moving to an archive whose
# SHA-256 has not been reviewed and pinned here.
_KNOWN_ARCHIVE_SHA256S = {
    "build-tools;36.1.0|windows|https://dl.google.com/android/repository/build-tools_r36.1_windows.zip": "23189d2d52b40a070a05e9cf7e497c9563f67fee76902e8fd3135ef29ef4dbeb",
    "cmdline-tools;latest|windows|https://dl.google.com/android/repository/commandlinetools-win-14742923_latest.zip": "cc610ccbe83faddb58e1aa68e8fc8743bb30aa5e83577eceb4cc168dae95f9ee",
    "emulator|windows|https://dl.google.com/android/repository/emulator-windows_x64-15261927.zip": "e768552aed01356784c71ad26b6493340101cea56f38d17396efea638248f024",
    "ndk;29.0.14206865|windows|https://dl.google.com/android/repository/android-ndk-r29-windows.zip": "4f83a1a87ea0d33ae2b43812ce27b768be949bc78acf90b955134d19e3068f1c",
    "platform-tools|windows|https://dl.google.com/android/repository/platform-tools_r37.0.0-win.zip": "4fe305812db074cea32903a489d061eb4454cbc90a49e8fea677f4b7af764918",
    "platforms;android-36|all|https://dl.google.com/android/repository/platform-36_r02.zip": "37607369a28c5b640b3a7998868d45898ebcb777565a0e85f9acf36f29631d2e",
    "system-images;android-36;google_apis;x86_64|all|https://dl.google.com/android/repository/sys-img/google_apis/x86_64-36_r07.zip": "b1bb0769d0bed7698e61f203d7dc9bf6e7c37cd01a39d0d8788a11186bc78160",
}

def _archive_key(path, host_os, url):
    return "{}|{}|{}".format(path, host_os, url)

def _extract_between(text, open_tag, close_tag, start = 0):
    """Return (inner, end_pos) for the first `<open_tag>...<close_tag>` after
    `start`, or `(None, -1)` if not found."""
    open_idx = text.find(open_tag, start)
    if open_idx < 0:
        return None, -1
    inner_start = open_idx + len(open_tag)
    close_idx = text.find(close_tag, inner_start)
    if close_idx < 0:
        return None, -1
    return text[inner_start:close_idx], close_idx + len(close_tag)

def _extract_tag(text, tag):
    """Return the content of the first `<tag>...</tag>` in `text`, or None."""
    inner, _ = _extract_between(text, "<" + tag + ">", "</" + tag + ">")
    return inner

def _parse_archives(block):
    """Yield `{host_os, url}` dicts for every `<archive>...</archive>` in
    `block`. `host_os` is `None` when the archive element has no `<host-os>`
    child (OS-independent packages such as `platforms`)."""
    out = []
    cursor = 0
    for _ in range(1024):  # Starlark requires a bounded loop.
        inner, next_cursor = _extract_between(block, "<archive>", "</archive>", cursor)
        if inner == None:
            break
        url = _extract_tag(inner, "url")
        if not url:
            cursor = next_cursor
            continue
        host_os = _extract_tag(inner, "host-os")
        out.append({"host_os": host_os, "url": url.strip()})
        cursor = next_cursor
    return out

def _api_num(k):
    return int(k[len("android-"):]) if k.startswith("android-") else -1

def _version_key(v):
    parts = []
    for chunk in v.replace("-", ".").split("."):
        if chunk.isdigit():
            parts.append((0, int(chunk)))
        else:
            parts.append((1, chunk))
    return parts

def _merge_manifest(ctx, index, xml_url, local_name, base_url):
    """Fetch one repository XML manifest and merge its `<remotePackage>`
    entries into `index`. `base_url` is prepended to each archive's relative
    `<url>` to form the absolute download URL."""
    ctx.download(
        url = [xml_url],
        output = local_name,
        allow_fail = False,
    )
    text = ctx.read(local_name)

    chunks = text.split("<remotePackage ")
    for chunk in chunks[1:]:
        path_marker = 'path="'
        p_start = chunk.find(path_marker)
        if p_start < 0:
            continue
        p_start += len(path_marker)
        p_end = chunk.find('"', p_start)
        if p_end < 0:
            continue
        path = chunk[p_start:p_end]

        close_idx = chunk.find("</remotePackage>")
        if close_idx < 0:
            continue
        block = chunk[:close_idx]

        if ";" in path:
            category, version = path.split(";", 1)
        else:
            # Singleton packages such as `platform-tools` are modelled as
            # version "latest" so downstream lookup is uniform.
            category, version = path, "latest"

        if category not in index:
            continue

        archives = _parse_archives(block)
        if not archives:
            continue

        per_os = {}
        for arch in archives:
            host_os = arch["host_os"] or "all"
            url = base_url + arch["url"]
            sha256 = _KNOWN_ARCHIVE_SHA256S.get(_archive_key(path, host_os, url))
            if sha256 == None:
                continue
            per_os[host_os] = struct(url = url, sha256 = sha256)
        if not per_os:
            continue
        index[category][version] = per_os

def _seed_index_from_allowlist(index):
    """Inject every pinned archive from `_KNOWN_ARCHIVE_SHA256S` into `index`.

    The allowlist — not the manifest — is the real pin. Google de-lists older
    archives from its live manifests (notably singleton packages such as
    `emulator`, whose `latest` entry is bumped in place) even though the files
    stay downloadable indefinitely. Seeding the index from the allowlist keeps
    those reviewed pins resolvable no matter what the current manifest
    advertises. Manifest entries merged earlier win (identical URL+SHA), so this
    only adds archives the manifest no longer lists.
    """
    for key, sha256 in _KNOWN_ARCHIVE_SHA256S.items():
        path, host_os, url = key.split("|")
        if ";" in path:
            category, version = path.split(";", 1)
        else:
            # Singleton packages (emulator, platform-tools, cmdline-tools) are
            # modelled as version "latest", matching `_merge_manifest`.
            category, version = path, "latest"
        if category not in index:
            continue
        versions = index[category]
        per_os = versions.get(version)
        if per_os == None:
            per_os = {}
            versions[version] = per_os
        if host_os not in per_os:
            per_os[host_os] = struct(url = url, sha256 = sha256)

def build_index(ctx):
    index = {cat: {} for cat in _CATEGORIES}

    # Top-level manifest: build-tools, platforms, platform-tools,
    # cmdline-tools, ndk, emulator, ...
    _merge_manifest(ctx, index, REPO_XML_URL, "repository2-3.xml", REPO_BASE)

    # Per-tag system-image manifests. Each is keyed by a `<tag>` (android,
    # google_apis, google_apis_playstore) and its archive URLs are relative
    # to that manifest's directory, not REPO_BASE.
    for tag in _SYS_IMG_TAGS:
        sub_base = REPO_BASE + "sys-img/" + tag + "/"
        _merge_manifest(
            ctx,
            index,
            sub_base + "sys-img2-3.xml",
            "sys-img-{}.xml".format(tag),
            sub_base,
        )

    # Pinned archives are the source of truth; make sure none were dropped just
    # because Google no longer lists them in the current manifests.
    _seed_index_from_allowlist(index)

    return index

def resolve_version(index, category, version, host_os):
    """Look up a URL for (category, version) restricted to host_os.

    Returns `struct(url, sha256, resolved_version)`. Entries without a known
    SHA-256 are filtered out while building the index.
    """
    bucket = index.get(category)
    if not bucket:
        fail("//build/rules/android: no '{}' entries with known SHA-256 in repository manifests".format(category))

    if category == "platforms":
        if version == "latest":
            version = sorted(bucket.keys(), key = _api_num)[-1]
        per_os = bucket.get(version)
        if per_os == None:
            fail("//build/rules/android: platform '{}' not in repository manifests with known SHA-256. Candidates: {}".format(
                version, sorted(bucket.keys()),
            ))
        rec = per_os.get("all") or per_os.get(host_os)
        if rec == None:
            fail("//build/rules/android: platform '{}' has no archive with known SHA-256. OSes: {}".format(
                version, sorted(per_os.keys()),
            ))
        return struct(url = rec.url, sha256 = rec.sha256, resolved_version = version)

    if version == "latest":
        candidates = [v for v in bucket.keys() if host_os in bucket[v] or "all" in bucket[v]]
        if not candidates:
            fail("//build/rules/android: no '{}' package for host '{}' with known SHA-256 in repository manifests".format(category, host_os))
        version = sorted(candidates, key = _version_key)[-1]

    per_os = bucket.get(version)
    if per_os == None:
        fail("//build/rules/android: '{}' version '{}' not in repository manifests with known SHA-256. Candidates: {}".format(
            category, version, sorted(bucket.keys(), key = _version_key),
        ))
    rec = per_os.get(host_os) or per_os.get("all")
    if rec == None:
        fail("//build/rules/android: '{}' version '{}' has no '{}' archive with known SHA-256. Available OSes: {}".format(
            category, version, host_os, sorted(per_os.keys()),
        ))
    return struct(url = rec.url, sha256 = rec.sha256, resolved_version = version)
