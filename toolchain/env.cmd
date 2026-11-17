@echo off
rem Puts the portable VC++ 2005 SP1 toolchain (toolchain\msvc8sp1) on PATH/INCLUDE/LIB for this shell only.
set "VC8=%~dp0msvc8sp1\VC"
set "PATH=%VC8%\bin;%~dp0msvc8sp1\Common7\IDE;%VC8%\PlatformSDK\Bin;%PATH%"
set "INCLUDE=%VC8%\include;%VC8%\atlmfc\include;%VC8%\PlatformSDK\Include"
set "LIB=%VC8%\lib;%VC8%\atlmfc\lib;%VC8%\PlatformSDK\Lib"
