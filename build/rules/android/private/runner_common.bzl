"""Shared helpers for rule-generated Python runners."""

def rlocation_path(file):
    """Return the runfiles key used for a File.

    External repository files have short paths like
    `../+android+android_sdk/platform-tools/adb.exe`; runfiles manifests store
    those without the leading `../`. Main-repository files are left relative to
    the workspace and resolved by the generated runner with a `_main/` fallback.
    """
    short_path = file.short_path
    if short_path.startswith("../"):
        return short_path[3:]
    return short_path

_WINDOWS_LAUNCHER_TEMPLATE = """@echo off
setlocal EnableExtensions EnableDelayedExpansion

if not defined RUNFILES_MANIFEST_FILE if exist "%~f0.runfiles_manifest" set "RUNFILES_MANIFEST_FILE=%~f0.runfiles_manifest"
if not defined RUNFILES_DIR if exist "%~f0.runfiles" set "RUNFILES_DIR=%~f0.runfiles"

set "PY_RLOCATION=__PYTHON_RLOCATION__"
set "SCRIPT_RLOCATION=__SCRIPT_RLOCATION__"

call :rlocation "%PY_RLOCATION%" PYTHON_EXE
if errorlevel 1 exit /b 1
call :rlocation "%SCRIPT_RLOCATION%" RUNNER_SCRIPT
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" "%RUNNER_SCRIPT%" %*
exit /b %ERRORLEVEL%

:rlocation
set "KEY=%~1"
set "OUTVAR=%~2"

if defined RUNFILES_DIR (
    set "CANDIDATE=%RUNFILES_DIR%\\%KEY:/=\\%"
    if exist "!CANDIDATE!" (
        set "%OUTVAR%=!CANDIDATE!"
        exit /b 0
    )
    if not "%KEY:~0,6%"=="_main/" (
        set "CANDIDATE=%RUNFILES_DIR%\\_main\\%KEY:/=\\%"
        if exist "!CANDIDATE!" (
            set "%OUTVAR%=!CANDIDATE!"
            exit /b 0
        )
    )
)

if defined RUNFILES_MANIFEST_FILE (
    for /f "usebackq tokens=1,* delims= " %%A in ("%RUNFILES_MANIFEST_FILE%") do (
        if "%%A"=="%KEY%" (
            set "%OUTVAR%=%%B"
            exit /b 0
        )
        if not "%KEY:~0,6%"=="_main/" if "%%A"=="_main/%KEY%" (
            set "%OUTVAR%=%%B"
            exit /b 0
        )
    )
)

echo runfiles: cannot resolve %KEY% 1>&2
exit /b 1
"""

_POSIX_LAUNCHER_TEMPLATE = """#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${RUNFILES_MANIFEST_FILE:-}" && -f "$0.runfiles_manifest" ]]; then
  export RUNFILES_MANIFEST_FILE="$0.runfiles_manifest"
fi
if [[ -z "${RUNFILES_DIR:-}" && -d "$0.runfiles" ]]; then
  export RUNFILES_DIR="$0.runfiles"
fi

PY_RLOCATION='__PYTHON_RLOCATION__'
SCRIPT_RLOCATION='__SCRIPT_RLOCATION__'

rlocation() {
  local key="$1"
  local candidate
  if [[ -n "${RUNFILES_DIR:-}" ]]; then
    candidate="${RUNFILES_DIR}/${key}"
    if [[ -e "${candidate}" ]]; then
      printf '%s\\n' "${candidate}"
      return 0
    fi
    if [[ "${key}" != _main/* ]]; then
      candidate="${RUNFILES_DIR}/_main/${key}"
      if [[ -e "${candidate}" ]]; then
        printf '%s\\n' "${candidate}"
        return 0
      fi
    fi
  fi
  if [[ -n "${RUNFILES_MANIFEST_FILE:-}" ]]; then
    local resolved
    resolved="$(awk -v key="${key}" '$1 == key { sub($1 " ", ""); print; exit }' "${RUNFILES_MANIFEST_FILE}")"
    if [[ -n "${resolved}" ]]; then
      printf '%s\\n' "${resolved}"
      return 0
    fi
    if [[ "${key}" != _main/* ]]; then
      resolved="$(awk -v key="_main/${key}" '$1 == key { sub($1 " ", ""); print; exit }' "${RUNFILES_MANIFEST_FILE}")"
      if [[ -n "${resolved}" ]]; then
        printf '%s\\n' "${resolved}"
        return 0
      fi
    fi
  fi
  echo "runfiles: cannot resolve ${key}" >&2
  return 1
}

PYTHON_EXE="$(rlocation "${PY_RLOCATION}")"
RUNNER_SCRIPT="$(rlocation "${SCRIPT_RLOCATION}")"
exec "${PYTHON_EXE}" "${RUNNER_SCRIPT}" "$@"
"""

def write_python_launcher(ctx, name, python_executable, runner_script):
    """Declare and write the executable wrapper for a generated Python runner."""
    substitutions = {
        "__PYTHON_RLOCATION__": rlocation_path(python_executable),
        "__SCRIPT_RLOCATION__": rlocation_path(runner_script),
    }
    is_windows = ctx.target_platform_has_constraint(
        ctx.attr._windows_constraint[platform_common.ConstraintValueInfo],
    )

    if is_windows:
        launcher = ctx.actions.declare_file(name + ".cmd")
        content = _WINDOWS_LAUNCHER_TEMPLATE
    else:
        launcher = ctx.actions.declare_file(name)
        content = _POSIX_LAUNCHER_TEMPLATE

    for key, value in substitutions.items():
        content = content.replace(key, value)

    ctx.actions.write(
        output = launcher,
        content = content,
        is_executable = True,
    )
    return launcher
