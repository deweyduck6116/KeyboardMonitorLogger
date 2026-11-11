@echo off
setlocal
cd /d "%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if exist "%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" (
 echo ERROR: Windows .NET Framework C# compiler not found.
 pause
 exit /b 1
)
"%CSC%" /nologo /target:winexe /platform:anycpu /optimize+ /out:"KeyMonitor_Combined.exe" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "KeyMonitor_Combined.cs"
if errorlevel 1 (
 echo BUILD FAILED.
 pause
 exit /b 1
)
echo Built KeyMonitor_Combined.exe
pause
