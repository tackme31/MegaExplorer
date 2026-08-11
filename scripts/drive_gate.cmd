@echo off
rem Writes (or clears) the flag that lets ui_shot.py drive -- which hijacks the
rem real mouse and keyboard -- run WITHOUT being approved each time. It means
rem "nobody is here to answer a prompt", not "permission": cleared, drive still
rem works, the PreToolUse hook in .claude/settings.local.json just asks first.
rem Only the unattended /evolve loop needs the flag, and it skips drive entirely
rem while the flag is absent rather than stalling on a prompt no one will answer.
rem
rem A flag file rather than an environment variable: setx never reaches an
rem already-running process, so the Claude Code session and the hooks spawned
rem from it would keep seeing the old value until the session is restarted --
rem which means tearing down the loop.
rem
rem Always expires. Leaving it on overnight and losing the mouse the next
rem morning is the accident this guards against.
rem
rem Usage: drive_gate.cmd <hours>   (0 or "off" clears it)
setlocal
set "FLAG=%LOCALAPPDATA%\MegaExplorerLoop\drive-allowed"

if "%~1"=="" goto :usage
if /i "%~1"=="off" goto :clear
if "%~1"=="0" goto :clear

rem Not Get-Date -UFormat %%s: on Windows PowerShell 5.1 that is seconds since
rem the epoch in *local* time, so the expiry landed 9h late here (JST).
for /f %%i in ('powershell -NoProfile -Command "[DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + %~1 * 3600"') do set "EXPIRY=%%i"
if not exist "%LOCALAPPDATA%\MegaExplorerLoop" mkdir "%LOCALAPPDATA%\MegaExplorerLoop"
> "%FLAG%" echo %EXPIRY%

for /f "delims=" %%d in ('powershell -NoProfile -Command "(Get-Date).AddHours(%~1).ToString('yyyy-MM-dd HH:mm')"') do set "WHEN=%%d"
echo drive: unattended until %WHEN%  (%~1h, no prompt)
echo.
echo   Do NOT lock the workstation -- drive's input goes to the secure desktop
echo   while locked and never reaches the app. Turning the display off is fine.
echo.
"%SystemRoot%\System32\timeout.exe" /t 6 >nul
goto :eof

:clear
if exist "%FLAG%" del /q "%FLAG%"
echo drive: back to asking each time (the loop skips it)
"%SystemRoot%\System32\timeout.exe" /t 3 >nul
goto :eof

:usage
echo usage: drive_gate.cmd ^<hours^>   ^(or "off"^)
exit /b 2
