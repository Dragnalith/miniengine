# Architecture

## Modules

- `foundation`: layer above the platform; owns the entry point; abstracts window, input, thread, mutex, and similar services; on Windows, owns a dedicated window thread that pulls messages; public headers should not expose platform headers.
- `rhi`: minimal GPU abstraction over DX12 or Vulkan; exposes only what the rest of the engine needs.
- `framework`: defines the frame concept; owns the frame manager; schedules update and rendering work; contains reusable engine systems such as the renderer.
- `app`: defines application behavior.

## State Boundaries

- Global mutable state is not authorized in miniengine modules.
- `foundation` is the only exception: it may own process-wide platform state needed to implement windowing, input, threading, synchronization, and other platform services.
- Foundation state is internally mutable but externally read-only. Public foundation APIs must be observational queries: calling them must not update, advance, drain, or consume state.
- Repeated immediate reads, such as two consecutive `GetState()` calls with no intervening platform progress, should return the same data.
- Other modules receive state through explicit ownership, references, handles, or read-only foundation queries.

## Headers

- Each module separates public headers from private headers.
- Public headers are included as `#include <module_name/my_header.h>`.
- Private headers are included as `#include <private/header.h>`.
- Build include dirs must not expose private include dirs to dependents.

## Frame Pipeline

- Stage 1: game update.
- Stage 2: renderer prepares command buffers.
- Stage 3: kick submits GPU work.
- Stages are interleaved across frames.
