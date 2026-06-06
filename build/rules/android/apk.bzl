"""Public entry point for the android_apk rule.

Usage:
    load("//build/rules/android:apk.bzl", "android_apk")

    android_apk(
        name           = "my_app",
        manifest       = "AndroidManifest.xml",
        custom_package = "com.example.my_app",
        deps           = [":my_cc_lib"],
        assets         = ["//shaders:vert_spv"],
        default_emulator = ":emulator",
    )

    bazel run //:my_app
    bazel run //:my_app -- --device
    bazel run //:my_app -- --emulator=emulator-5554
    bazel run //:my_app -- log
    bazel run //:my_app -- list
"""

load("//build/rules/android/private:apk_impl.bzl", _android_apk = "android_apk")

android_apk = _android_apk
