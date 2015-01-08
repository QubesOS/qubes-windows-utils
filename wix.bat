@ECHO OFF

IF "%_BUILDARCH%"=="x86" (SET DIFXLIB="%WIX%\bin\difxapp_x86.wixlib") ELSE (SET DIFXLIB="%WIX%\bin\difxapp_x64.wixlib")

IF "%_BUILDARCH%"=="x86" (SET MSIARCH=x86) ELSE (SET MSIARCH=x64)

IF "%DDKBUILDENV%"=="chk" (SET MSIBUILD=_debug) ELSE (SET MSIBUILD=)

SET MSIOS=%DDK_TARGET_OS%
IF "%DDK_TARGET_OS%"=="WinLH" (SET MSIOS=Vista2008)
IF "%DDK_TARGET_OS%"=="Win7" (SET MSIOS=Win7)

SET MSINAME=windows-utils-%MSIOS%%MSIARCH%%MSIBUILD%.msm

"%WIX%\bin\candle" installer.wxs -arch %MSIARCH% -ext "%WIX%\bin\WixUIExtension.dll" -ext "%WIX%\bin\WixDifxAppExtension.dll" -ext "%WIX%\bin\WixIIsExtension.dll"
"%WIX%\bin\light.exe" -o %MSINAME% installer.wixobj %DIFXLIB% -ext "%WIX%\bin\WixUIExtension.dll" -ext "%WIX%\bin\WixDifxAppExtension.dll" -ext "%WIX%\bin\WixIIsExtension.dll"
