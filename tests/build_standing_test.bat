@echo off
rem Builds the headless standing tests (core sources only). Sources: this checkout (any worktree of mod/).
rem Output: %OUT%\standing_test.exe, default ..\..\build next to this checkout (set OUT to keep a worktree's build apart).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set SRC=%~dp0..
if "%OUT%"=="" set OUT=%~dp0..\..\build
if not exist %OUT%\standing_obj mkdir %OUT%\standing_obj
cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /I%SRC%\include ^
  %SRC%\src\core\Core.cpp %SRC%\src\core\Ltx.cpp %SRC%\src\core\Config.cpp %SRC%\src\core\Standing.cpp ^
  %SRC%\tests\standing_test.cpp /Fe:%OUT%\standing_test.exe /Fo:%OUT%\standing_obj\
exit /b %errorlevel%
