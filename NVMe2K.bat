@echo off

rem NVMe2K Driver build script for W10 x64 + W2K Alpha + NT4 PPC (one day)
rem
rem  V1.00  RH   26-Nov-2025  Initial Release, Note: Alpha not as yet tested
rem  V1.01  RH   28-Nov-2025  Change tagget directory from w2k to WDM
rem  V1.02  RH   14-Dec-2025  Update to build W2K RC2 Alpha to W2K and NT4 PowerPC
rem

rem ==================== Common Startup ====================

set __BUILDMACHINE__=\\%COMPUTERNAME%
set BUILDOK=Failed to build
set WDM=WDM
set W2K=W2K

if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" goto do_xnn
if /i "%PROCESSOR_ARCHITECTURE%"=="x86"   goto do_xnn
if /i "%PROCESSOR_ARCHITECTURE%"=="ALPHA" goto do_axp
if /i "%PROCESSOR_ARCHITECTURE%"=="PPC"   goto do_ppc

echo PROCESSOR_ARCHITECTURE %PROCESSOR_ARCHITECTURE% is not supported
goto end

rem ==================== All Three WDM ====================

:do_xnn

set BASEREL=7600.16385.1
set MAKEX32=WXP

set BASEREL=6001.18001
set MAKEX32=W2K

set MAKEX64=WNET
set MAKEI64=WNET
                                      set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\%WDM%

if not exist ..\obj             mkdir ..\obj
if not exist ..\obj\%WDM%       mkdir ..\obj\%WDM%
if not exist ..\obj\%WDM%\amd64 mkdir ..\obj\%WDM%\amd64
if not exist ..\obj\%WDM%\i386  mkdir ..\obj\%WDM%\i386
if not exist ..\obj\%WDM%\ia64  mkdir ..\obj\%WDM%\ia64

if exist ..\obj\%WDM%\amd64\_objects.mac del ..\obj\%WDM%\amd64\_objects.mac
if exist  ..\obj\%WDM%\i386\_objects.mac del  ..\obj\%WDM%\i386\_objects.mac
if exist  ..\obj\%WDM%\ia64\_objects.mac del  ..\obj\%WDM%\ia64\_objects.mac


rem ==================== x32 WDM Build ====================

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre     %MAKEX32% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Kx32
popd
endlocal

if ERRORLEVEL 1 goto no_kit

copy              ..\obj\%WDM%\i386\NVMe2Kx32.sys .

rem ==================== x64 WDM Build ====================

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre x64 %MAKEX64% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Kx64
popd
endlocal

if ERRORLEVEL 1 goto no_kit

copy             ..\obj\%WDM%\amd64\NVMe2Kx64.sys .

rem ==================== i64 WDM Build ====================

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% fre  64 %MAKEI64% bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj\%WDM%
build %1 -M 1 -g -P -j ..\obj\%WDM%\NVMe2Ki64
popd
endlocal

if ERRORLEVEL 1 goto no_kit

copy              ..\obj\%WDM%\ia64\NVMe2Ki64.sys .

popd

set WDKDIR= + %BASEDIR%

rem ==================== x32 NT4 Setup ====================

set BASEREL=1381
set MSTOOLS=C:\Program Files (x86)\Microsoft Visual Studio

                                      set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\NT4

if not exist  .\i386            mkdir  .\i386
if not exist ..\obj             mkdir ..\obj
if not exist ..\obj\i386        mkdir ..\obj\i386
if not exist ..\obj\i386\free   mkdir ..\obj\i386\free

if exist  ..\obj\i386\_objects.mac del  ..\obj\i386\_objects.mac
if exist   ..\obj\ppc\_objects.mac del   ..\obj\ppc\_objects.mac

rem ==================== x32 NT4 Build ====================

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
popd
popd
endlocal

if ERRORLEVEL 1 goto no_kit

if exist  .\obj\_objects.mac del   .\obj\_objects.mac
if exist  .\obj\i386         rmdir .\obj\i386
if exist  .\obj              rmdir .\obj

copy                               ..\obj\i386\free\NVMe2K.sys .\i386\

set BUILDOK=WDM Built OK

goto no_kit

rem ==================== a32 W2K Setup ====================

:do_axp

set BASEREL=2128

if                                    set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\%W2K%

if not exist ..\obj            mkdir ..\obj       
if not exist ..\obj\alpha      mkdir ..\obj\alpha
if not exist      .\alpha      mkdir      .\alpha

rem ==================== a32 W2K Build ====================

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% free    %W2K%  bscmake
popd
pushd .
set BUILD_ALT_DIR=\..\..\obj
build %1  -M 1 -P -j ..\obj\NVMe2Ka32
popd
endlocal

if ERRORLEVEL 1 goto no_kit

copy     ..\obj\alpha\NVMe2K.sys   .\alpha

if exist  .\obj\_objects.mac del   .\obj\_objects.mac
if exist  .\obj              rmdir .\obj

set BUILDOK=AXP Built OK

goto no_kit

rem ==================== p32 NT4 Setup ====================

:do_ppc

set BASEREL=1381
set MSTOOLS=C:\MSDEV\bin

                                      set BASEDIR=C:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=D:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=E:\WINDDK\%BASEREL%
if not exist %BASEDIR%\bin\setenv.bat set BASEDIR=L:\WINDDK\%BASEREL%

if not exist %BASEDIR%\bin\setenv.bat goto no_wdk

pushd .\NT4

if not exist  .\ppc                  mkdir  .\ppc
if not exist ..\obj                  mkdir ..\obj
if not exist ..\obj\ppc              mkdir ..\obj\ppc
if not exist ..\obj\ppc\free         mkdir ..\obj\ppc\free

if     exist ..\obj\ppc\_objects.mac del   ..\obj\ppc\_objects.mac

rem ==================== p32 NT4 Build ====================

setlocal
pushd .
set NO_BROWSER_FILE=
call %BASEDIR%\bin\setenv.bat %BASEDIR% free
popd
pushd .
set PATH=%PATH%;%BASEDIR%\bin
set BUILD_ALT_DIR=\..\..\obj
%BASEDIR%\bin\build %1 -M 1 -j ..\obj\NVMe2Kp32
popd
endlocal

if ERRORLEVEL 1 goto no_kit

if exist  .\obj\_objects.mac del   .\obj\_objects.mac
if exist  .\obj\ppc          rmdir .\obj\ppc
if exist  .\obj              rmdir .\obj

copy                               ..\obj\ppc\free\NVMe2K.sys .\ppc\

set BUILDOK=PPC Built OK

goto no_kit

rem ==================== Common Ending ====================

:no_kit
title %BUILDOK% on %__BUILDMACHINE__% using the Windows Driver Kits from %BASEDIR%%WDKDIR%
@echo %BUILDOK% on %__BUILDMACHINE__% using the Windows Driver Kits from %BASEDIR%%WDKDIR%

goto do_pop

:no_wdk
echo Unable to locate a Windows %BASEREL% Driver Kit on %__BUILDMACHINE__%
:do_pop
popd
:end

rem ==================== Common Cleanup ====================

set __BUILDMACHINE__=
set BUILDOK=
set BASEREL=
set BASEDIR=
set MAKEX32=
set MAKEX64=
set MAKEI64=
set MSTOOLS=
set WDM=
set W2K=
set WDKDIR=
goto :eof
