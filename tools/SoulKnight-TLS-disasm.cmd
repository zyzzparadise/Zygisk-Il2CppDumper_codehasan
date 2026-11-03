@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Drag your libil2cpp.so file onto this CMD script.
  echo Or run: SoulKnight-TLS-disasm.cmd "C:\path\libil2cpp.so"
  pause
  exit /b 2
)
where py >nul 2>&1
if errorlevel 1 (
  echo Python launcher "py" was not found.
  pause
  exit /b 2
)
py -c "import capstone" >nul 2>&1
if errorlevel 1 (
  echo Installing Capstone ARM64 disassembler...
  py -m pip install --user capstone
  if errorlevel 1 (
    echo Failed to install Capstone. See the error above.
    pause
    exit /b 1
  )
)
py "%~dp0soulknight_tls_arm64_disasm.py" "%~1"
echo.
echo The result will be next to this CMD file: SoulKnight-TLS-disasm.txt
pause
