# Working on Mini Engine

This guide defines how to approach any task in this repository, from intent to verification.

Read `README.md` first: it documents how to build, run, and control the app, and is the source of truth for the exact commands.

## 1. Respect the architecture

Every solution must follow the principles in `ARCHITECTURE.md`. They come first and shape what a correct solution looks like.

If a principle seems too strict and relaxing it would yield a materially simpler solution, do not silently bend it: explain your reasoning and agree on the trade-off with the user before proceeding.

## 2. Implement the change

Make the smallest change that fully satisfies the task while staying consistent with the surrounding code and the architecture.

## 3. Prove it works

Code review alone does not finish a task. An app task is complete only after you have built, run, and exercised the app to confirm the new behavior — always do this unless the user explicitly tells you not to test. Test on Android by default (`--platforms=//:android`); only target Windows when the task is Windows-specific or the user asks.

1. Build and run in one step with `bazel run //src/app --platforms=//:android`.
2. Drive the running app with `bazel run //tools:control_target` — replay input and capture screenshots, then inspect those screenshots to confirm the behavior your task targeted.

Write any files produced for debugging (screenshots, logs, scratch output) into the `_tmp/` folder — it is gitignored, so these artifacts stay out of the repository.

Treat a task as done only once this verification passes.
