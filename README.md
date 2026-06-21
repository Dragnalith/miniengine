# Mini Engine

Mini Game Engine reproduce the structure of a professional game engine in order to demonstrate architecture and infrastructure topics.

## Build

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
