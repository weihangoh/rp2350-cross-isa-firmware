@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM connect-debug-probe-wsl.bat
REM Attach Raspberry Pi Debug Probe / CMSIS-DAP USB device to WSL2 using usbipd-win.

set "PATTERN=Raspberry Pi Debug Probe CMSIS-DAP Picoprobe Debug Probe"

echo Checking usbipd...
where usbipd >nul 2>nul
if errorlevel 1 (
    echo [ERROR] usbipd was not found.
    echo Install usbipd-win first:
    echo   https://github.com/dorssel/usbipd-win
    exit /b 1
)

echo Keeping WSL awake...
wsl.exe -- bash -lc "true" >nul 2>nul

echo.
echo Current USB devices:
usbipd list
echo.

set "BUSID="

REM Try to find likely Debug Probe / CMSIS-DAP device.
for /f "tokens=1,* delims= " %%A in ('usbipd list ^| findstr /I /C:"Raspberry Pi Debug Probe" /C:"CMSIS-DAP" /C:"Picoprobe" /C:"Debug Probe"') do (
    if not defined BUSID (
        set "BUSID=%%A"
        set "DEVLINE=%%A %%B"
    )
)

if not defined BUSID (
    echo [ERROR] Could not automatically find Raspberry Pi Debug Probe / CMSIS-DAP.
    echo.
    set /p BUSID=Enter BUSID manually from the usbipd list above, e.g. 2-4: 
    if not defined BUSID (
        echo [ERROR] No BUSID provided.
        exit /b 1
    )
) else (
    echo Selected device:
    echo   !DEVLINE!
    echo BUSID:
    echo   !BUSID!
)

echo.
echo Sharing device with usbipd bind...
echo If this fails with access denied, rerun this batch file as Administrator.
usbipd bind --busid !BUSID! >nul 2>nul

echo.
echo Attaching device to WSL2...
usbipd attach --wsl --busid !BUSID!
if errorlevel 1 (
    echo.
    echo [ERROR] usbipd attach failed.
    echo Try running:
    echo   usbipd list
    echo   usbipd bind --busid !BUSID!
    echo   usbipd attach --wsl --busid !BUSID!
    exit /b 1
)

echo.
echo Checking device from inside WSL2...
wsl.exe -- bash -lc "lsusb | grep -Ei 'Raspberry|CMSIS|DAP|Debug|Picoprobe' || lsusb"

echo.
echo Done. Debug Probe should now be visible inside WSL2.
echo.
echo To detach later, run:
echo   usbipd detach --busid !BUSID!
echo.

endlocal
