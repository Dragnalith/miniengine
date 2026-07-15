@echo off
setlocal

rem Downloads Bazelisk to the repo root as bazel.exe (zero dependencies: uses in-box curl.exe).
rem Bazelisk reads .bazelversion to fetch the matching Bazel on first run.

set "BAZELISK_VERSION=v1.27.0"
set "TARGET=%~dp0bazel.exe"
set "URL=https://github.com/bazelbuild/bazelisk/releases/download/%BAZELISK_VERSION%/bazelisk-windows-amd64.exe"

if exist "%TARGET%" (
    echo bazel: already installed at %TARGET%
    exit /b 0
)

curl.exe -fsSL -o "%TARGET%" "%URL%"
if errorlevel 1 (
    echo bazel: download FAILED from %URL%
    exit /b 1
)

echo bazel: installed Bazelisk %BAZELISK_VERSION% at %TARGET%
exit /b 0
