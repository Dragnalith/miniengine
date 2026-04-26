"""Providers and path helpers for game asset rules."""

AssetSpecInfo = provider(
    doc = "Describes asset files and their bundle-relative destinations.",
    fields = {
        "entries": "depset of struct(file = File, dest = string).",
    },
)

def validate_relative_path(path, what):
    """Validates a forward-slash relative file path."""
    if path == "":
        fail("%s must not be empty" % what)
    if "\\" in path:
        fail("%s must use forward slashes: %s" % (what, path))
    if path.startswith("/"):
        fail("%s must be relative and must not start with '/': %s" % (what, path))
    if path.endswith("/"):
        fail("%s must name a file and must not end with '/': %s" % (what, path))
    if ":" in path:
        fail("%s must be relative and must not contain ':': %s" % (what, path))

    parts = path.split("/")
    for part in parts:
        if part == "":
            fail("%s must not contain empty path segments: %s" % (what, path))
        if part == "." or part == "..":
            fail("%s must not contain '.' or '..' path segments: %s" % (what, path))

def validate_relative_dir(path, what):
    """Validates a forward-slash relative directory path; empty means root."""
    if path == "":
        return
    if "\\" in path:
        fail("%s must use forward slashes: %s" % (what, path))
    if path.startswith("/"):
        fail("%s must be relative and must not start with '/': %s" % (what, path))
    if path.endswith("/"):
        fail("%s must not end with '/': %s" % (what, path))
    if ":" in path:
        fail("%s must be relative and must not contain ':': %s" % (what, path))

    parts = path.split("/")
    for part in parts:
        if part == "":
            fail("%s must not contain empty path segments: %s" % (what, path))
        if part == "." or part == "..":
            fail("%s must not contain '.' or '..' path segments: %s" % (what, path))

def join_relative_path(prefix, path):
    if prefix == "":
        return path
    return prefix + "/" + path
