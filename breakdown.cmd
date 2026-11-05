@echo off
rem breakdown: the project CLI (breakdown.py). Python 3.9 or newer.
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "%~dp0breakdown.py" %*
) else (
    python "%~dp0breakdown.py" %*
)
exit /b %errorlevel%
