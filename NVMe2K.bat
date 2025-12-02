@echo off

rem NVMe2K Driver build script for W10 x64 + W2K Alpha + NT4 PPC (one day)
rem
rem  V1.00  RH   26-Nov-2025  Initial Release, Note: Alpha not as yet tested
rem  V1.01  RH   28-Nov-2025  Change tagget directory from w2k to WDM
rem

set __BUILDMACHINE__=\\%COMPUTERNAME%
set BUILDOK=Failed to build
set WDM=WDM

if "%PROCESSOR_ARCHITECTURE%"=="AMD64" goto do_xnn
if "%PROCESSOR_ARCHITECTURE%"=="x86"   goto do_xnn
if "%PROCESSOR_ARCHITECTURE%"=="ALPHA" goto do_axp
rem if "%PROCESSOR_ARCHITECTURE%"=="PPC"   goto do_ppc

echo PROCESSOR_ARCHITECTURE %PROCESSOR_ARCHITECTURE% is not supported
goto end

:do_xnn

set BASEREL=7600.16385.1
set MAKEX32=WXP

set BASEREL=6001.18001
set MAKEX32=W2K

set MAKEX64=WNET
set MAKEI64=WNET
                                      set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\%WDM%

if not exist ..\obj             mkdir ..\obj
if not exist ..\obj\%WDM%       mkdir ..\obj\%WDM%
if not exist ..\obj\%WDM%\alpha mkdir ..\obj\%WDM%\alpha
if not exist ..\obj\%WDM%\amd64 mkdir ..\obj\%WDM%\amd64
if not exist ..\obj\%WDM%\i386  mkdir ..\obj\%WDM%\i386
if not exist ..\obj\%WDM%\ia64  mkdir ..\obj\%WDM%\ia64

if exist  ..\obj\%WDM%\i386\_objects.mac del  ..\obj\%WDM%\i386\_objects.mac
if exist ..\obj\%WDM%\amd64\_objects.mac del ..\obj\%WDM%\amd64\_objects.mac
if exist  ..\obj\%WDM%\ia64\_objects.mac del  ..\obj\%WDM%\ia64\_objects.mac

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre     %MAKEX32% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Kx32
copy              ..\obj\%WDM%\i386\NVMe2Kx32.sys .
popd
endlocal

if ERRORLEVEL 1 goto no_kit

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre x64 %MAKEX64% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Kx64
popd
copy             ..\obj\%WDM%\amd64\NVMe2Kx64.sys .
endlocal

if ERRORLEVEL 1 goto no_kit

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre  64 %MAKEX64% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Ki64
copy              ..\obj\%WDM%\ia64\NVMe2Ki64.sys .
popd
endlocal

if ERRORLEVEL 1 goto no_kit

popd

set WDKDIR= + %BASEDIR%

set BASEREL=1381
set MSTOOLS=C:\Program Files (x86)\Microsoft Visual Studio

                                      set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\NT4

if not exist ..\obj             mkdir ..\obj
if not exist ..\obj\alpha       mkdir ..\obj\alpha
if not exist ..\obj\alpha\free  mkdir ..\obj\alpha\free
if not exist ..\obj\i386        mkdir ..\obj\i386
if not exist ..\obj\i386\free   mkdir ..\obj\i386\free
if not exist ..\obj\ppc         mkdir ..\obj\ppc
if not exist ..\obj\ppc\free    mkdir ..\obj\ppc\free

if exist ..\obj\alpha\_objects.mac del ..\obj\alpha\_objects.mac
if exist  ..\obj\i386\_objects.mac del  ..\obj\i386\_objects.mac
if exist   ..\obj\ppc\_objects.mac del   ..\obj\ppc\_objects.mac

setlocal
pushd .
set PROCESSOR_ARCHITECTURE=x86
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% free
popd
pushd .
set PATH=%PATH%;%BASEDIR%\bin\i386\free
set BUILD_ALT_DIR=\..\..\obj
%BASEDIR%\bin\i386\free\build %1 -cZ -M 1 -j ..\obj\NVMe2Kx32
copy                               ..\obj\i386\free\NVMe2Kx32.sys .
popd
popd
endlocal

if ERRORLEVEL 1 goto no_kit

set BUILDOK=Built OK

goto no_kit

:do_axp

set BASEREL=2128

if          "%BASEDIR%"== ""          set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\%WDM%

if not exist    obj           mkdir    obj
if not exist    obj\alpha     mkdir    obj\alpha

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre     W2K  bscmake
popd
pushd .
build %1  -M 1 -g -j ..\obj\%WDM%\NVMe2Ka32
popd
endlocal

if ERRORLEVEL 1 goto no_kit

popd
pushd .\wnt

if not exist    obj           mkdir    obj
if not exist  . obj\alpha     mkdir    obj\alpha

set BUILDOK=Built OK

goto no_kit

:no_kit
title %BUILDOK% on %__BUILDMACHINE__% using the Windows Driver Kits from %BASEDIR%%WDKDIR%
echo %BUILDOK% on %__BUILDMACHINE__% using the Windows Driver Kits from %BASEDIR%%WDKDIR%

goto do_pop

:no_wdk
echo Unable to locate a Windows %BASEREL% Driver Kit on %__BUILDMACHINE__%
:do_pop
popd
:end
goto :eof
