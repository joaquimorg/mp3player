@echo off
rem Converte um link do YouTube (ou um ficheiro de audio) em MP3 e envia-o para o relogio.
rem Uso interativo: youtube-para-relogio.bat
rem Uso por argumentos: youtube-para-relogio.bat "<link-ou-ficheiro>" <slot 1-4> <ip>
rem Variaveis de ambiente para execucao automatica: NOPAUSE=1 (nao fazer pause no fim) e TWATCH_NOCHCP=1.
rem O pause final so acontece em duplo clique, arrastar-e-largar ou modo interativo.
rem Codigos de saida: 0 ok, 1 dados invalidos, 2 relogio inacessivel, 3 falha na conversao,
rem                   4 audio demasiado longo, 5 falha no envio.
rem Nota: nao se usa "enabledelayedexpansion" de proposito - assim "!" e "%" num link nao sao
rem       interpretados; "&" funciona porque os valores estao sempre entre aspas.
if not defined TWATCH_NOCHCP chcp 65001 >nul
setlocal disabledelayedexpansion
set "DIR=%~dp0"
set "DIRNB=%DIR:~0,-1%"
set "RC=0"
set "WORK="
set "MAXB=3145728"
set "INTERATIVO="
if "%~1"=="" set "INTERATIVO=1"
set "CL=%CMDCMDLINE:"=%"
set "CL2=%CL:/c=%"
set "PAUSAR="
if defined INTERATIVO set "PAUSAR=1"
if not "%CL2%"=="%CL%" set "PAUSAR=1"
if defined NOPAUSE set "PAUSAR="
if defined TWATCH_NOCHCP set "PAUSAR="
set "SRC=%~1"
set "SLOT=%~2"
set "IP=%~3"
set "APAGADO="
set "LASTIP="
if exist "%DIR%ultimo-ip.txt" set /p "LASTIP="<"%DIR%ultimo-ip.txt"

echo.
echo === YouTube para o relógio T-Watch ===
echo.

rem ---------- pedir/validar a origem ----------
if defined SRC goto :srcok
set /p "SRC=Link do YouTube (ou caminho de um ficheiro de áudio): "
:srcok
if not defined SRC goto :srcvazio
set "SRC=%SRC:"=%"
if not defined SRC goto :srcvazio
set "TIPO="
set "P7=%SRC:~0,7%"
set "P8=%SRC:~0,8%"
if /i "%P7%"=="http://" set "TIPO=url"
if /i "%P8%"=="https://" set "TIPO=url"
if not defined TIPO if exist "%SRC%" set "TIPO=ficheiro"
if not defined TIPO goto :srcmau
if "%TIPO%"=="url" if not exist "%DIR%yt-dlp.exe" goto :semferr
if not exist "%DIR%ffmpeg.exe" goto :semferr
goto :slotarg
:srcvazio
echo ERRO: não indicaste nenhum link nem ficheiro.
set "RC=1" & goto :fim
:srcmau
echo ERRO: "%SRC%" não é um link http^(s^) nem um ficheiro que exista.
set "RC=1" & goto :fim
:semferr
echo ERRO: faltam o yt-dlp.exe ou o ffmpeg.exe. Corre primeiro o obter-ferramentas.bat.
set "RC=1" & goto :fim

rem ---------- slot dado por argumento ----------
:slotarg
if not defined SLOT goto :iparg
set "SLOT=%SLOT:"=%"
call :validaslot
if errorlevel 1 goto :slotmau

rem ---------- IP ----------
:iparg
if defined IP goto :ipok
if defined INTERATIVO goto :ippedir
set "IP=%LASTIP%"
if defined IP goto :ipok
echo ERRO: indica o IP do relógio ^(3.º argumento^).
set "RC=1" & goto :fim
:ippedir
if defined LASTIP set /p "IP=IP do relógio (aparece no ecrã, em Programação) [%LASTIP%]: "
if not defined LASTIP set /p "IP=IP do relógio (aparece no ecrã, em Programação): "
if not defined IP set "IP=%LASTIP%"
:ipok
set "IP=%IP:"=%"
call :validaip
if errorlevel 1 goto :ipmau

rem ---------- pasta temporaria ----------
set "WORK=%TEMP%\twatch-%RANDOM%%RANDOM%"
set "TW_WORK=%WORK%"
mkdir "%WORK%\dl" >nul 2>&1
if errorlevel 1 goto :semtemp

rem ---------- falar com o relogio ----------
echo A contactar o relógio em %IP% ...
call :lerslots
if errorlevel 1 goto :inacessivel
> "%DIR%ultimo-ip.txt" echo %IP%
echo.
echo Slots no relógio:
call :mostraslots

if defined SLOT goto :converter
set /p "SLOT=Slot de destino (1-4): "
if not defined SLOT goto :slotmau
set "SLOT=%SLOT:"=%"
call :validaslot
if errorlevel 1 goto :slotmau
call :lerslots
if errorlevel 1 goto :inacessivel

rem ---------- converter ----------
:converter
echo.
set "MP3="
if "%TIPO%"=="url" goto :baixar
for %%F in ("%SRC%") do set "NOME=%%~nF"
set "T=%NOME%"
set "T=%T:,=%"
set "T=%T:;=%"
if not "%T%"=="%NOME%" set "NOME=faixa"
echo A converter o ficheiro para MP3 ^(44,1 kHz, 112 kbps^) ...
"%DIR%ffmpeg.exe" -hide_banner -loglevel error -y -i "%SRC%" -vn -map_metadata -1 -ar 44100 -b:a 112k "%WORK%\dl\%NOME%.mp3"
if errorlevel 1 goto :convmau
goto :achar

:baixar
echo A descarregar e converter o áudio ^(pode demorar um pouco^) ...
"%DIR%yt-dlp.exe" --ffmpeg-location "%DIRNB%" --no-playlist --no-warnings --windows-filenames --replace-in-metadata title "[^\w .()'-]+" " " -x --audio-format mp3 --audio-quality 112K --postprocessor-args "ffmpeg:-ar 44100 -b:a 112k" -o "%WORK%\dl\%%(title).60s.%%(ext)s" -- "%SRC%"
if errorlevel 1 goto :convmau

:achar
for %%F in ("%WORK%\dl\*.mp3") do set "MP3=%%~fF"
if not defined MP3 goto :convmau
call :tamanho
echo Ficheiro MP3: %KB% KB
if %SIZE% LEQ %MAXB% goto :tamok

rem demasiado grande: tentar uma vez a 64 kbps mono
echo O áudio ocupa mais de 3 MB ^(%KB% KB^). A tentar de novo em 64 kbps mono ...
for %%F in ("%MP3%") do set "NOME=%%~nF"
mkdir "%WORK%\r2" >nul 2>&1
"%DIR%ffmpeg.exe" -hide_banner -loglevel error -y -i "%MP3%" -vn -ac 1 -ar 44100 -b:a 64k "%WORK%\r2\%NOME%.mp3"
if errorlevel 1 goto :convmau
set "MP3=%WORK%\r2\%NOME%.mp3"
if not exist "%MP3%" goto :convmau
call :tamanho
if %SIZE% GTR %MAXB% goto :longo
echo Resultado: %KB% KB ^(64 kbps mono - qualidade mais baixa para caber nos 3 MB^).
:tamok

rem ---------- espaco no relogio ----------
if %SIZE% GTR %FREE% goto :semespaco

:enviar
copy /y "%MP3%" "%WORK%\up.mp3" >nul
for %%F in ("%MP3%") do set "UPN=%%~nF"
set "UPN=%UPN:,=%"
set "UPN=%UPN:;=%"
echo.
echo A enviar %KB% KB para o slot %SLOT%. Aguarda: o envio demora cerca de 1 a 2 minutos
echo e o relógio fica ocupado. Não desligues nem toques no relógio.
curl.exe -sS -# --connect-timeout 10 -m 900 -H "Expect:" -F "file=@%WORK%\up.mp3;filename=%UPN%.mp3" -o "%WORK%\resp.json" -w "%%{http_code}" "http://%IP%/api/slot/%SLOT%" > "%WORK%\code.txt" 2> "%WORK%\err.txt"
set "CURLRC=%ERRORLEVEL%"
echo.
if not "%CURLRC%"=="0" goto :enviomau
set "CODE=000"
set /p "CODE="<"%WORK%\code.txt"
if "%CODE%"=="200" goto :enviook
findstr /c:"Sem espa" "%WORK%\resp.json" >nul 2>&1
if not errorlevel 1 goto :semespaco
echo ERRO: o relógio recusou o ficheiro ^(HTTP %CODE%^):
call :mostraerro
set "RC=5" & goto :fim

:enviook
echo Enviado com sucesso para o slot %SLOT%!
call :lerslots
if not errorlevel 1 call :mostraslots
echo No relógio: toca em "Reprodução" para ouvir.
goto :fim

:enviomau
echo ERRO: o envio falhou ^(curl código %CURLRC%^). O slot anterior mantém-se.
type "%WORK%\err.txt" 2>nul
echo Confirma que o relógio continua em "Programação" e na mesma rede Wi-Fi.
set "RC=5" & goto :fim

:semespaco
echo.
echo Sem espaço no relógio para este ficheiro ^(%KB% KB; livre: %FREE% bytes^).
if "%OLD%"=="0" goto :semespaco2
echo O slot %SLOT% já tem uma faixa; para a substituir é preciso espaço para a antiga e a nova.
echo Apaga primeiro este slot ^(ou outro^).
if defined APAGADO goto :semespaco3
if not defined INTERATIVO goto :semespaco3
set "R="
set /p "R=Apagar o slot %SLOT% agora e enviar? [S/N]: "
if /i not "%R%"=="S" goto :semespaco3
curl.exe -sS --connect-timeout 10 -m 30 -X DELETE -o "%WORK%\resp.json" "http://%IP%/api/slot/%SLOT%" >nul 2>&1
if errorlevel 1 goto :delmau
set "APAGADO=1"
echo Slot %SLOT% apagado.
call :lerslots
if errorlevel 1 goto :inacessivel
if %SIZE% GTR %FREE% goto :semespaco
goto :enviar
:delmau
echo ERRO: não consegui apagar o slot %SLOT%.
goto :semespaco3
:semespaco2
echo Apaga outro slot para libertar espaço.
:semespaco3
echo Para apagar um slot: curl -X DELETE http://%IP%/api/slot/N   ^(N = 1 a 4^)
set "RC=5" & goto :fim

:longo
echo ERRO: vídeo demasiado longo. Mesmo a 64 kbps mono o ficheiro tem %KB% KB e o máximo é 3072 KB
echo ^(cerca de 3,7 minutos a 112 kbps ou 6,5 minutos a 64 kbps^). Escolhe um vídeo mais curto.
set "RC=4" & goto :fim

:convmau
echo ERRO: não foi possível obter/converter o áudio. Se for um link do YouTube, o yt-dlp pode estar
echo desatualizado: corre "obter-ferramentas.bat atualizar" e tenta de novo.
set "RC=3" & goto :fim

:slotmau
echo ERRO: o slot deve ser 1, 2, 3 ou 4.
set "RC=1" & goto :fim

:ipmau
echo ERRO: o IP "%IP%" não é válido. Usa o formato 192.168.1.50 ^(como aparece no ecrã do relógio^).
set "RC=1" & goto :fim

:semtemp
echo ERRO: não consegui criar a pasta temporária em "%TEMP%".
set "WORK="
set "RC=1" & goto :fim

:inacessivel
echo.
echo ERRO: não consegui falar com o relógio em %IP%.
if exist "%WORK%\err.txt" type "%WORK%\err.txt" 2>nul
echo Verifica se:
echo   - o relógio está no modo "Programação" ^(toca em "Programar"; o ecrã mostra o IP^);
echo   - o PC e o relógio estão na mesma rede Wi-Fi;
echo   - o IP é o que aparece no ecrã do relógio.
set "RC=2" & goto :fim

:fim
if defined WORK if exist "%WORK%" rmdir /s /q "%WORK%" >nul 2>&1
if defined PAUSAR echo. & pause
endlocal & exit /b %RC%

rem ======================= sub-rotinas =======================

:validaslot
if "%SLOT%"=="1" exit /b 0
if "%SLOT%"=="2" exit /b 0
if "%SLOT%"=="3" exit /b 0
if "%SLOT%"=="4" exit /b 0
exit /b 1

:validaip
set "A=" & set "B=" & set "C=" & set "D=" & set "E="
for /f "delims=0123456789." %%x in ("%IP%") do exit /b 1
for /f "tokens=1-5 delims=." %%a in ("%IP%") do set "A=%%a" & set "B=%%b" & set "C=%%c" & set "D=%%d" & set "E=%%e"
if defined E exit /b 1
if not defined D exit /b 1
for %%o in (%A% %B% %C% %D%) do if %%o GTR 255 exit /b 1
exit /b 0

rem GET /api/slots -> %WORK%\slots.json e variaveis FREE (bytes livres) e OLD (tamanho do slot escolhido)
:lerslots
set "FREE=0"
set "OLD=0"
del "%WORK%\err.txt" "%WORK%\code.txt" "%WORK%\slots.json" >nul 2>&1
curl.exe -sS --connect-timeout 5 -m 20 -H "Expect:" -o "%WORK%\slots.json" -w "%%{http_code}" "http://%IP%/api/slots" > "%WORK%\code.txt" 2> "%WORK%\err.txt"
if errorlevel 1 exit /b 1
set "CODE=000"
set /p "CODE="<"%WORK%\code.txt"
if not "%CODE%"=="200" exit /b 1
set "TW_SL=0"
if defined SLOT set "TW_SL=%SLOT%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $j=Get-Content -Raw -Encoding UTF8 -LiteralPath ($env:TW_WORK+'\slots.json') | ConvertFrom-Json; $o=0; foreach($s in $j.slots){ if($s.n -eq $env:TW_SL){$o=$s.size} }; Set-Content -Encoding ASCII -LiteralPath ($env:TW_WORK+'\info.cmd') -Value ('set FREE='+[long]$j.free), ('set OLD='+[long]$o) } catch { exit 1 }" >nul 2>&1
if errorlevel 1 exit /b 1
call "%WORK%\info.cmd"
exit /b 0

:mostraslots
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Console]::OutputEncoding=[Text.Encoding]::UTF8; try { $j=Get-Content -Raw -Encoding UTF8 -LiteralPath ($env:TW_WORK+'\slots.json') | ConvertFrom-Json; foreach($s in $j.slots){ if($s.size -gt 0){$t=('{0,6:N0} KB  {1}' -f ($s.size/1024),$s.name)}else{$t='vazio'}; Write-Host ('  Slot {0}: {1}' -f $s.n,$t) }; Write-Host ('  Espaço livre: {0:N0} KB' -f ($j.free/1024)) } catch { Get-Content -Raw -LiteralPath ($env:TW_WORK+'\slots.json') }"
exit /b 0

:mostraerro
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Console]::OutputEncoding=[Text.Encoding]::UTF8; try { $j=Get-Content -Raw -Encoding UTF8 -LiteralPath ($env:TW_WORK+'\resp.json') | ConvertFrom-Json; Write-Host ('  ' + $j.error) } catch { Get-Content -Raw -LiteralPath ($env:TW_WORK+'\resp.json') }"
exit /b 0

:tamanho
set "SIZE=0"
for %%F in ("%MP3%") do set "SIZE=%%~zF"
if "%SIZE:~10,1%" NEQ "" set "SIZE=2147483647"
set /a KB=SIZE/1024
exit /b 0
