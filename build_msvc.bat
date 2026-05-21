@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d %~dp0

if not exist obj32 mkdir obj32

echo [1/3] Compile x86 no-CRT (single unit)
cl /O1 /GS- /Gy /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DINITGUID /DCOBJMACROS /DNDEBUG /nologo /c /Fo:obj32\all.obj src\all.c
if errorlevel 1 goto :err

echo [2/3] Link
link /SUBSYSTEM:WINDOWS /ENTRY:WinMain /NODEFAULTLIB /MERGE:.rdata=.text /FIXED /OPT:REF /OPT:ICF /PDB:NONE /OUT:codex-pet-raw.exe obj32\all.obj kernel32.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib uuid.lib msimg32.lib
if errorlevel 1 goto :err

echo [3/3] UPX compress
del codex-pet.exe 2>nul
upx --ultra-brute -o codex-pet.exe codex-pet-raw.exe
if errorlevel 1 goto :err

echo.
echo === Done ===
for %%f in (codex-pet.exe) do echo Size: %%~zf bytes
goto :end

:err
echo.
echo Build failed!
exit /b 1

:end
