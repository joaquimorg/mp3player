@echo off
rem Descarrega yt-dlp.exe e ffmpeg.exe para esta pasta (só é preciso uma vez).
rem   obter-ferramentas.bat            -> descarrega o que faltar
rem   obter-ferramentas.bat atualizar  -> volta a descarregar o yt-dlp.exe (o YouTube muda muitas vezes)
if not defined TWATCH_NOCHCP chcp 65001 >nul
setlocal disabledelayedexpansion
set "DIR=%~dp0"
set "RC=0"
set "WORK="
set "ATUALIZAR="
if /i "%~1"=="atualizar" set "ATUALIZAR=1"

where curl.exe >nul 2>&1
if errorlevel 1 (
  echo ERRO: o curl.exe não foi encontrado. Precisas do Windows 10/11 atualizado.
  set "RC=1"
  goto :fim
)
where tar.exe >nul 2>&1
if errorlevel 1 (
  echo ERRO: o tar.exe não foi encontrado. Precisas do Windows 10/11 atualizado.
  set "RC=1"
  goto :fim
)

set "WORK=%TEMP%\twatch-tools-%RANDOM%%RANDOM%"
mkdir "%WORK%" >nul 2>&1
if errorlevel 1 (
  echo ERRO: não consegui criar a pasta temporária "%WORK%".
  set "RC=1"
  goto :fim
)

rem ---------- yt-dlp ----------
if exist "%DIR%yt-dlp.exe" if not defined ATUALIZAR (
  echo [yt-dlp] já existe - nada a fazer. ^(Para atualizar: obter-ferramentas.bat atualizar^)
  goto :ffmpeg
)
echo [yt-dlp] a descarregar de github.com/yt-dlp/yt-dlp ...
curl.exe -L --fail --retry 2 --connect-timeout 20 -# -o "%WORK%\yt-dlp.exe" "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
if errorlevel 1 (
  echo ERRO: falhou o download do yt-dlp.exe. Verifica a ligação à Internet e tenta de novo.
  set "RC=2"
  goto :fim
)
call :tamanho "%WORK%\yt-dlp.exe" SZ
if %SZ% LSS 1000000 (
  echo ERRO: o yt-dlp.exe descarregado parece incompleto.
  set "RC=2"
  goto :fim
)
copy /y "%WORK%\yt-dlp.exe" "%DIR%yt-dlp.exe" >nul
if errorlevel 1 (
  echo ERRO: não consegui gravar o yt-dlp.exe em "%DIR%".
  set "RC=2"
  goto :fim
)
echo [yt-dlp] instalado.

:ffmpeg
rem ---------- ffmpeg ----------
if exist "%DIR%ffmpeg.exe" (
  echo [ffmpeg] já existe - nada a fazer.
  goto :versoes
)
echo [ffmpeg] a descarregar de www.gyan.dev ^(cerca de 80 MB, pode demorar^) ...
curl.exe -L --fail --retry 2 --connect-timeout 20 -# -o "%WORK%\ffmpeg.zip" "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip"
if errorlevel 1 (
  echo ERRO: falhou o download do ffmpeg. Verifica a ligação à Internet e tenta de novo.
  set "RC=3"
  goto :fim
)
echo [ffmpeg] a extrair ...
mkdir "%WORK%\x" >nul 2>&1
tar.exe -xf "%WORK%\ffmpeg.zip" -C "%WORK%\x" "*/bin/ffmpeg.exe" "*/bin/ffprobe.exe"
set "FOUND="
for /r "%WORK%\x" %%F in (ffmpeg.exe) do if exist "%%F" set "FOUND=%%F"
if not defined FOUND (
  echo ERRO: não encontrei o ffmpeg.exe dentro do ficheiro descarregado.
  set "RC=3"
  goto :fim
)
copy /y "%FOUND%" "%DIR%ffmpeg.exe" >nul
if errorlevel 1 (
  echo ERRO: não consegui gravar o ffmpeg.exe em "%DIR%".
  set "RC=3"
  goto :fim
)
echo [ffmpeg] instalado.

:versoes
echo.
echo ---- Versões instaladas ----
if exist "%DIR%yt-dlp.exe" (
  <nul set /p "=yt-dlp: "
  "%DIR%yt-dlp.exe" --version
) else (
  echo yt-dlp: NÃO instalado
  set "RC=2"
)
if exist "%DIR%ffmpeg.exe" (
  "%DIR%ffmpeg.exe" -version 2>&1 | findstr /b /c:"ffmpeg version"
) else (
  echo ffmpeg: NÃO instalado
  set "RC=3"
)

:fim
if defined WORK if exist "%WORK%" rmdir /s /q "%WORK%" >nul 2>&1
if "%RC%"=="0" (echo. & echo Pronto. Agora podes usar youtube-para-relogio.bat.)
if "%~1"=="" if not defined NOPAUSE pause
endlocal & exit /b %RC%

:tamanho
set "%~2=%~z1"
exit /b 0
