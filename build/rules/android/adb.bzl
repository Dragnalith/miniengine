"""Public entry point for the `adb` wrapper rule.

Usage:
    load("//build/rules/android:adb.bzl", "adb")

    adb(name = "adb")

    bazel run //path/to:adb -- devices
    bazel run //path/to:adb -- -s emulator-5554 shell getprop
    bazel run //path/to:adb -- install path/to/app.apk
"""

load("//build/rules/android/private:adb_impl.bzl", _adb = "adb")

adb = _adb
