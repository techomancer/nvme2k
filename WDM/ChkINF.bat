@setlocal
@pushd .

@set BASEREL=7600.16385.1
@set BASEREL=6001.18001
@set ChkDIR=tools\ChkINF
@set ChkINF=%ChkDIR%\ChkINF.bat

@                                set BASEDIR=C:\WINDDK\%BASEREL%
@if not exist %BASEDIR%\%ChkINF% set BASEDIR=C:\WINDDK\%BASEREL%
@if not exist %BASEDIR%\%ChkINF% set BASEDIR=D:\WINDDK\%BASEREL%
@if not exist %BASEDIR%\%ChkINF% set BASEDIR=E:\WINDDK\%BASEREL%
@if not exist %BASEDIR%\%ChkINF% set BASEDIR=L:\WINDDK\%BASEREL%

@if not exist %BASEDIR%\%ChkINF% goto no_wdk

@if exist HTM del /s /q HTM

@set INFDIR=%1
@if ""=="%INFDIR%" set INFDIR=.

%BASEDIR%\%ChkINF%  /B %INFDIR% %2 %3 %4 %5 %6

@goto end

:no_wdk
@echo Unable to locate a Windows %BASEREL% Driver Kit on %__BUILDMACHINE__%
:end
@popd
@endlocal
@goto :eof
