# Microsoft Developer Studio Project File - Name="NVMe2K" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=NVMe2K - Win32
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "NVMe2K.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "NVMe2K.mak" CFG="NVMe2K - Win32"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "NVMe2K - Win32" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe
# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "."
# PROP BASE Intermediate_Dir "."
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir ""
# PROP Intermediate_Dir "OBJ"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /YX /c
# ADD CPP /nologo /G6 /MT /W3 /WX /Gm /GX /Zi /O1 /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /FR /FD /c
# SUBTRACT CPP /YX /Yc /Yu
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG" /d SXS_TARGET="""trim.exe"""
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo /o".\obj\trim.bsc"
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib wininet.lib /nologo /subsystem:console /incremental:no /debug /machine:I386 /out:"trim.exe"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=NVMe2K.bat
# End Special Build Tool
# Begin Target

# Name "NVMe2K - Win32"
# Begin Group "Trim"

# PROP Default_Filter ".c"
# Begin Source File

SOURCE=.\trim\trim.c
# End Source File
# End Group
# Begin Group "NVMe2K"

# PROP Default_Filter ".c"
# Begin Source File

SOURCE=.\nvme.h
# End Source File
# Begin Source File

SOURCE=.\nvme2k.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\nvme2k.h
# End Source File
# Begin Source File

SOURCE=.\nvme2k.rc
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\nvme2k_cpl.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\nvme2k_nvme.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\nvme2k_scsi.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\scsiext.h
# End Source File
# Begin Source File

SOURCE=.\utils.c
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\utils.h
# End Source File
# End Group
# Begin Group "Build WDM"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\NVMe2K.bat
# End Source File
# Begin Source File

SOURCE=.\WDM\NVMe2K.ddf
# End Source File
# Begin Source File

SOURCE=.\WDM\NVMe2K.inf
# End Source File
# Begin Source File

SOURCE=.\WDM\sources
# End Source File
# End Group
# Begin Group "Build NT4"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\nt4\SOURCES
# End Source File
# End Group
# End Target
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;cnt;rtf;gif;jpg;jpeg;jpe"
# End Group
# End Project
