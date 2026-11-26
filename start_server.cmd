@echo off
setlocal
cd /d "%~dp0tea_sense_server"
if not exist ".venv\Scripts\python.exe" (
 echo ERROR: Missing tea_sense_server virtual environment. Rebuild .venv using requirements-dev.txt.
 exit /b 1
)
".venv\Scripts\python.exe" -m app.main
exit /b %errorlevel%
