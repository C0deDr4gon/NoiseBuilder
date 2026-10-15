@echo off
rem Usage: build.bat [release|debug|test] [avx2|avx512|sse2]
rem   test = build bin\noisebuilder_tests.exe (release flags). Does not run it.
rem Run from "x64 Native Tools Command Prompt" (cl.exe in PATH).
setlocal enabledelayedexpansion
cd /d "%~dp0"

set MODE=%1
if "%MODE%"=="" set MODE=release
set SIMD=%2
if "%SIMD%"=="" set SIMD=avx2

set FLAGS=/nologo /std:c++17 /EHsc /W4 /Isrc
set CFLAGS=/nologo /std:c11 /fp:precise
if /i "%MODE%"=="release" (
  set FLAGS=!FLAGS! /O2 /DNDEBUG
  set CFLAGS=!CFLAGS! /O2
) else if /i "%MODE%"=="test" (
  set FLAGS=!FLAGS! /O2 /DNDEBUG
  set CFLAGS=!CFLAGS! /O2
) else if /i "%MODE%"=="debug" (
  set FLAGS=!FLAGS! /Od /Zi
  set CFLAGS=!CFLAGS! /Od /Zi
) else (
  echo unknown mode: %MODE% ^(release^|debug^|test^)
  exit /b 1
)

if /i "%SIMD%"=="avx512" (
  set FLAGS=!FLAGS! /arch:AVX512
  set CFLAGS=!CFLAGS! /arch:AVX512
) else if /i "%SIMD%"=="avx2" (
  set FLAGS=!FLAGS! /arch:AVX2
  set CFLAGS=!CFLAGS! /arch:AVX2
) else if /i "%SIMD%"=="sse2" (
  rem x64 default is SSE2, no flag
) else (
  echo unknown simd: %SIMD% ^(avx2^|avx512^|sse2^)
  exit /b 1
)

set SRC=
for /r src %%f in (*.cpp) do (
  if /i "%MODE%"=="test" (
    if /i not "%%~nxf"=="main.cpp" set SRC=!SRC! "%%f"
  ) else (
    set SRC=!SRC! "%%f"
  )
)

if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj
echo cl !CFLAGS!
cl !CFLAGS! /c src\io\libtex.c /Fobin\obj\libtex.obj
if errorlevel 1 exit /b 1
echo cl !FLAGS!
if /i "%MODE%"=="test" (
  cl !FLAGS! !SRC! tests\tests.cpp bin\obj\libtex.obj /Fobin\obj\ /Febin\noisebuilder_tests.exe
  if errorlevel 1 exit /b 1
  echo ok: bin\noisebuilder_tests.exe
) else (
  cl !FLAGS! !SRC! bin\obj\libtex.obj /Fobin\obj\ /Febin\noisebuilder.exe
  if errorlevel 1 exit /b 1
  echo ok: bin\noisebuilder.exe
)
