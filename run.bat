@echo off
echo Starting Gateway OS2...
echo.

:: Create userdata disk if it doesn't exist (persists credentials across boots)
if not exist "%~dp0userdata.img" (
    echo Creating userdata disk...
    "C:\msys64\mingw64\bin\qemu-img.exe" create -f raw "%~dp0userdata.img" 1M
)

:: Start mail relay in background
start /B "" "C:\msys64\mingw64\bin\python.exe" "%~dp0mail_relay.py" > nul 2>&1

:: Give relay a moment to start
timeout /t 1 /nobreak > nul

:: Launch QEMU with userdata disk as IDE slave
"C:\msys64\mingw64\bin\qemu-system-i386.exe" -kernel "%~dp0gateway2.elf" -m 128M -serial stdio -vga std -device e1000,netdev=net0 -netdev user,id=net0 -drive file="%~dp0userdata.img",format=raw,if=ide,index=1

:: When QEMU closes, kill the relay
taskkill /F /IM python.exe > nul 2>&1
