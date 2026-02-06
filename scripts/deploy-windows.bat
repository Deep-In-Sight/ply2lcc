@echo off
REM Deploy script wrapper for Windows
REM Usage: deploy-windows.bat [-Zip]

pushd %~dp0
powershell -ExecutionPolicy Bypass -File "%~dp0deploy-windows.ps1" %*
popd
