"""Shared archive extraction helpers for repository rules.

Android SDK archives typically wrap their payload in a single top-level
directory (e.g. `build-tools_r34-linux.zip` → `android-14/...`). We extract
to a scratch dir and promote the single child to the desired destination.
"""

def _is_windows(ctx):
    return "windows" in ctx.os.name.lower()

def _move(ctx, src, dst):
    src_s = str(src)
    dst_s = str(dst)
    if _is_windows(ctx):
        # `cmd /c move` fails with "Access is denied" when moving directories
        # whose content Bazel just extracted (it holds short-lived handles).
        # `robocopy /MOVE` handles this and is the documented Windows primitive
        # for directory moves. Exit codes 0-7 indicate success; >= 8 is a real
        # failure.
        res = ctx.execute([
            "cmd",
            "/c",
            "robocopy",
            src_s.replace("/", "\\"),
            dst_s.replace("/", "\\"),
            "/E",
            "/MOVE",
            "/NFL",
            "/NDL",
            "/NJH",
            "/NJS",
            "/NP",
        ])
        if res.return_code >= 8:
            fail("//build/rules/android: failed to move '{}' -> '{}': rc={} {}".format(
                src_s,
                dst_s,
                res.return_code,
                res.stderr,
            ))
    else:
        res = ctx.execute(["mv", src_s, dst_s])
        if res.return_code != 0:
            fail("//build/rules/android: failed to move '{}' -> '{}': {}".format(src_s, dst_s, res.stderr))

def copy_tree(ctx, src, dst):
    src_s = str(ctx.path(src))
    dst_s = str(ctx.path(dst))
    if _is_windows(ctx):
        res = ctx.execute([
            "cmd", "/c", "xcopy", "/E", "/I", "/Q", "/Y",
            src_s.replace("/", "\\"),
            dst_s.replace("/", "\\"),
        ])
    else:
        res = ctx.execute(["cp", "-r", src_s, dst_s])
    if res.return_code != 0:
        fail("//build/rules/android: failed to copy '{}' -> '{}': {}".format(src_s, dst_s, res.stderr))

def extract_flattened(ctx, url, sha256, dst, tmp_name, archive_type = "zip"):
    ctx.report_progress("Fetching {}".format(url.rsplit("/", 1)[-1]))
    kwargs = {
        "url": url,
        "output": tmp_name,
        "type": archive_type,
    }
    if sha256:
        kwargs["sha256"] = sha256
    ctx.download_and_extract(**kwargs)

    children = ctx.path(tmp_name).readdir()

    # Ensure the parent of `dst` exists and `dst` itself does not, so
    # `move`/`mv` can rename the source onto that path.
    sentinel = dst + "/.placeholder"
    ctx.file(sentinel, "")
    ctx.delete(sentinel)
    dst_path = ctx.path(dst)
    ctx.delete(str(dst_path))

    if len(children) == 1:
        _move(ctx, children[0], dst_path)
        ctx.delete(tmp_name)
    else:
        _move(ctx, ctx.path(tmp_name), dst_path)
