@echo off
setlocal
cd /d "%~dp0"
gcc -O2 -s -municode -mwindows -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE -Wall -o aviutl2-open-last.exe aviutl2-open-last.c -lshlwapi -lole32 -luuid
if errorlevel 1 (
  echo build failed
  exit /b 1
)
echo built aviutl2-open-last.exe
