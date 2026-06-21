# Mini Engine

Mini Game Engine reproduce the structure of a professional game engine in order to demonstrate architecture and infrastructure topics.

## The game

The app is a **memory match** game: a grid of face-down cards hiding pairs of matching symbols. The goal is to clear the board by finding every pair.

Basic controls:

- Tap a card to select and flip it face up, then tap a second card to select it.
- With two cards selected, tap to validate: if they match, the pair stays revealed.
- If they do not match, tap to flip both cards back face down, then try again.

## Build

There is nothing to install. Every dependency, toolchain, and SDK (MSVC, the Windows SDK, the Android SDK/NDK, etc.) is managed and fetched automatically by Bazel — you only need Bazel itself.

`//src/app` dispatches by platform; onec Windows the graphics API is `--//build/config:gpuapi` (`DX12` default, or `Vulkan`). On Android the graphics API is always `Vulkan`.

```shell
bazel build //src/app                                     # Windows, DX12
bazel build //src/app --//build/config:gpuapi=Vulkan      # Windows, Vulkan
bazel build //src/app --platforms=//:android              # Android (Vulkan)
```

Use `build` only when you just want to compile. To run, skip it: `run` builds and runs in one step.

## Run

```shell
bazel run //src/app                                       # Windows, DX12
bazel run //src/app --//build/config:gpuapi=Vulkan        # Windows, Vulkan
bazel run //src/app --platforms=//:android                # Android (Vulkan)
```

By default Android picks the connected USB device if one is present, otherwise it starts the emulator. Force a target with `-- --device` (USB device) or `-- --emulator` (emulator); use `-- list` / `-- log` to inspect or stream logs.

```shell
bazel run //src/app --platforms=//:android -- --device     # force USB device
bazel run //src/app --platforms=//:android -- --emulator   # force emulator
```

## Control the target

`//tools:control_target` drives the connected target (physical device preferred, else emulator) to take screenshots and replay input. Output paths are relative to where you run the command, and existing files are overwritten.

Input coordinates are in device pixels, matching the resolution printed at launch by `bazel run //src/app --platforms=//:android` (e.g. `device resolution: 1080x2410`). Screenshots are not always captured at the display resolution, so always scale any coordinates read off a screenshot back to the device resolution before using them.

```shell
bazel run //tools:control_target -- screenshot out.png
bazel run //tools:control_target -- input "TAP 40,570; WAIT 200; TAP 100,150; SCREENSHOT out.png"
bazel run //tools:control_target -- input --file gestures.txt
```

Input stream commands (`;`-separated, verbs case-insensitive, run in order):

- `TAP x,y` — tap at pixel `(x, y)`.
- `SWIPE x1,y1,x2,y2[,ms]` — swipe/drag between two points over `ms` milliseconds (default 300).
- `WAIT ms` — pause the script for `ms` milliseconds (no device call).
- `TEXT some text` — type text into the focused field (spaces preserved; alphanumeric only).
- `KEY <keycode>` — send one key event (see keycodes below).
- `SCREENSHOT path` — capture a screenshot to `path` mid-stream (overwritten if it exists).

Add `--serial <serial>` to target a specific device.

`KEY` accepts any Android keycode; common ones: `KEYCODE_HOME`, `KEYCODE_BACK`, `KEYCODE_APP_SWITCH`, `KEYCODE_ENTER`, `KEYCODE_TAB`, `KEYCODE_DEL`, `KEYCODE_ESCAPE`, `KEYCODE_MENU`, `KEYCODE_POWER`, `KEYCODE_VOLUME_UP`, `KEYCODE_VOLUME_DOWN`, `KEYCODE_DPAD_UP`, `KEYCODE_DPAD_DOWN`, `KEYCODE_DPAD_LEFT`, `KEYCODE_DPAD_RIGHT`, `KEYCODE_DPAD_CENTER`.
