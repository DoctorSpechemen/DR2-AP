@echo off
rem Puts the game's original data files back. Close the game first.
setlocal
set "PY=python"
where python >nul 2>nul || set "PY=py -3"
%PY% "%~dp0patch_game.py" --restore %*
echo.
pause
