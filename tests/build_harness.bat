@echo off
rem Builds the headless harness (core sources only - no CommonLibSSE). Sources: this checkout (any worktree of mod/).
rem Output: %OUT%\harness.exe, default ..\..\build next to this checkout (set OUT to keep a worktree's build apart).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set SRC=%~dp0..
if "%OUT%"=="" set OUT=%~dp0..\..\build
if not exist %OUT%\harness_obj mkdir %OUT%\harness_obj
cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /I%SRC%\include ^
  %SRC%\src\core\Core.cpp %SRC%\src\core\Ltx.cpp %SRC%\src\core\Config.cpp %SRC%\src\core\World.cpp %SRC%\src\core\Board.cpp %SRC%\src\core\Activities.cpp %SRC%\src\core\Standing.cpp ^
  %SRC%\tests\harness.cpp /Fe:%OUT%\harness.exe /Fo:%OUT%\harness_obj\
exit /b %errorlevel%
