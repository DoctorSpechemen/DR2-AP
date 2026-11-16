@echo off
rem Patches your own copy of Dead Rising 2 for DR2AP. Close the game first. Needs Python 3 (python.org).
rem Drag your game folder onto this file if the game is not in the usual Steam place.
setlocal
set "PY=python"
where python >nul 2>nul || set "PY=py -3"
%PY% "%~dp0patch_game.py" %*
echo.
pause
