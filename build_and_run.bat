@echo off
setlocal EnableExtensions

REM Safe Windows example for compiling legacy EVE code.
REM Configure these environment variables for YOUR installation:
REM   EVECC_DIR        = folder with evecc.exe, Python\, and keys
REM   EVE_SOURCE_DIR   = root with carbon\ and eve\ source trees
REM   EVECC_PASSWORD   = password used for evecc genkeys
REM   EVELOADER2_SCRIPT_DIR (optional) = output location for compiled.code
REM
REM This example NEVER deletes client DLLs and NEVER starts a game/launcher.
REM Use only on a private client copy, and back up existing compiled.code.

if not defined EVECC_DIR (
    echo [ERROR] Set EVECC_DIR to the folder containing evecc.exe
    exit /b 2
)
if not defined EVE_SOURCE_DIR (
    echo [ERROR] Set EVE_SOURCE_DIR to the folder containing carbon and eve
    exit /b 2
)
if not defined EVECC_PASSWORD (
    echo [ERROR] Set EVECC_PASSWORD before compiling
    exit /b 2
)

if not exist "%EVECC_DIR%\evecc.exe" (
    echo [ERROR] Cannot find "%EVECC_DIR%\evecc.exe"
    exit /b 2
)
if not exist "%EVE_SOURCE_DIR%\carbon\client" (
    echo [ERROR] Missing "%EVE_SOURCE_DIR%\carbon\client"
    exit /b 2
)
if not exist "%EVE_SOURCE_DIR%\eve\client" (
    echo [ERROR] Missing "%EVE_SOURCE_DIR%\eve\client"
    exit /b 2
)

pushd "%EVECC_DIR%" || exit /b 2
"%EVECC_DIR%\evecc.exe" compilecode --password "%EVECC_PASSWORD%" -o "compiled.code" ^
  -I "%EVE_SOURCE_DIR%\carbon\client" ^
  -I "%EVE_SOURCE_DIR%\carbon\common" ^
  -I "%EVE_SOURCE_DIR%\eve\common" ^
  -I "%EVE_SOURCE_DIR%\eve\client"
if errorlevel 1 (
    echo [ERROR] EVECC compilecode failed. No files were deployed.
    popd
    exit /b 3
)
popd

if defined EVELOADER2_SCRIPT_DIR (
    if not exist "%EVELOADER2_SCRIPT_DIR%" (
        echo [ERROR] Output directory does not exist: "%EVELOADER2_SCRIPT_DIR%"
        exit /b 2
    )
    if exist "%EVELOADER2_SCRIPT_DIR%\compiled.code" (
        echo [ERROR] Destination compiled.code exists; back it up manually.
        exit /b 2
    )
    copy /Y "%EVECC_DIR%\compiled.code" "%EVELOADER2_SCRIPT_DIR%\compiled.code" >nul
    if errorlevel 1 (
        echo [ERROR] Failed copying compiled.code
        exit /b 3
    )
    echo [OK] Compiled code copied to configured destination.
) else (
    echo [OK] Compilation complete: "%EVECC_DIR%\compiled.code"
    echo To deploy, back up your client output first and set EVELOADER2_SCRIPT_DIR.
)

exit /b 0
