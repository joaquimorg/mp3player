@echo off
rem Compila e testa a app T-Watch MP3 com o compilador de C# que já vem no Windows
rem (.NET Framework 4.x). Não precisa de instalar nada.
rem   compilar.bat           testes da lógica + app (TWatchMP3.exe)
rem   compilar.bat testes    só os testes da lógica
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo ERRO: nao encontrei o compilador csc.exe do .NET Framework 4.x.
  exit /b 1
)
if not exist bin mkdir bin

echo [1/2] A compilar e a correr os testes da logica...
"%CSC%" /nologo /warnaserror+ /out:bin\logictests.exe AppLogic.cs tests\LogicTests.cs
if errorlevel 1 exit /b 1
bin\logictests.exe
if errorlevel 1 (
  echo ERRO: os testes falharam.
  exit /b 1
)
if /i "%~1"=="testes" exit /b 0

echo [2/2] A compilar a app...
"%CSC%" /nologo /warnaserror+ /target:winexe /out:TWatchMP3.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll *.cs
if errorlevel 1 exit /b 1
echo OK: TWatchMP3.exe
exit /b 0
