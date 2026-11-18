@echo off
rem Installs BlockNudge into the Enshrouded game folder. Double-click it, or run it from a
rem terminal with options: install.cmd -Uninstall, -ResetIni, -Force, -GameDir "path".
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set result=%errorlevel%
rem Double-clicked: keep the window open so the outcome can be read.
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b %result%
