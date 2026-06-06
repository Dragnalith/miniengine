"""Compatibility entry point for the former `android_binary` macro.

New code should load and call `android_apk` from `//build/rules/android:apk.bzl`.
The compatibility symbol below is the same rule, not a macro.
"""

load("//build/rules/android:apk.bzl", _android_apk = "android_apk")

android_binary = _android_apk
