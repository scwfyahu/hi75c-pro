@echo off
setlocal
echo ==== Hi75C Pro diagnostic ====
echo.
echo -- OS --
wmic os get Caption,Version,OSArchitecture /value 2>nul | findstr "="
reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v EditionID 2>nul
echo.
echo -- Architecture --
echo PROCESSOR_ARCHITECTURE=%PROCESSOR_ARCHITECTURE%
echo.
echo -- Smart App Control (0=off 1=enforce 2=eval) --
reg query "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState 2>nul || echo   not present (good)
echo.
echo -- S mode check (if this fails, you are not in S mode) --
reg query "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v SkuPolicyRequired 2>nul
echo.
echo -- Integrity of the file you downloaded --
for %%F in ("%~dp0Hi75CPro.exe") do echo   %%~zF bytes  (expected 222720)
certutil -hashfile "%~dp0Hi75CPro.exe" SHA256 | findstr /v ":"
echo   expected ccb35d6b69c6a438cb4d4a5eb9abef4703a6b42b25336348d19fa12ec6b92a4f
echo.
echo -- Mark of the Web on this folder --
dir /r "%~dp0" 2>nul | findstr /i "Zone.Identifier" || echo   none (good)
echo.
echo ==== end ====
pause
