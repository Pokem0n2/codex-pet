@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d %~dp0

if not exist obj32 mkdir obj32

echo [1/3] Compile x86 no-CRT
cl /O1 /GS- /Gy /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DINITGUID /DCOBJMACROS /DNDEBUG /nologo /c /Fo:obj32\ src\main.c src\pet_json.c src\pet_io.c src\pet_render.c src\pet_ai.c src\pet_wnd.c src\pet_data.c src\crt_repl.c
if errorlevel 1 goto :err

echo [2/3] Link
link /SUBSYSTEM:WINDOWS /ENTRY:WinMain /NODEFAULTLIB /MERGE:.rdata=.text /FIXED /OPT:REF /OPT:ICF /PDB:NONE /OUT:codex-pet-raw.exe obj32\main.obj obj32\pet_json.obj obj32\pet_io.obj obj32\pet_render.obj obj32\pet_ai.obj obj32\pet_wnd.obj obj32\pet_data.obj obj32\crt_repl.obj kernel32.lib user32.lib gdi32.lib ole32.lib windowscodecs.lib uuid.lib msimg32.lib shell32.lib
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
