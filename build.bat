@echo off
REM build.bat - build VintageBlock.exe on Windows with MSVC or MinGW-w64.
REM
REM   MSVC  : run from a "x64 Native Tools Command Prompt for VS"
REM   MinGW : make sure g++ is on PATH
REM
REM Output: dist\VintageBlock.exe

setlocal
cd /d "%~dp0"
set SRC=src\vb_math.cpp src\vb_blocks.cpp src\vb_world.cpp src\vb_worldgen.cpp src\vb_mesh.cpp src\vb_game.cpp src\vb_font.cpp src\vb_render.cpp src\vb_ui.cpp src\vb_main.cpp
set LIBS=opengl32.lib gdi32.lib user32.lib

if not exist dist mkdir dist

where cl >nul 2>nul
if %errorlevel%==0 goto msvc
where g++ >nul 2>nul
if %errorlevel%==0 goto mingw
echo No C++ compiler found. Install Visual Studio (Desktop C++) or MinGW-w64.
exit /b 1

:msvc
echo ==^> building with MSVC
rc /nologo /fo app.res src\app.rc
if errorlevel 1 exit /b 1
cl /nologo /O2 /std:c++17 /EHsc /DNDEBUG /Fe:dist\VintageBlock.exe %SRC% app.res /link /SUBSYSTEM:WINDOWS %LIBS%
if errorlevel 1 exit /b 1
goto done

:mingw
echo ==^> building with MinGW-w64
windres src\app.rc -O coff -o app.res
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -mwindows %SRC% app.res -o dist\VintageBlock.exe -lopengl32 -lgdi32 -luser32 -static-libstdc++ -static-libgcc
if errorlevel 1 exit /b 1
goto done

:done
echo.
echo Built dist\VintageBlock.exe
endlocal
