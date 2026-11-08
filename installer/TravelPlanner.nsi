; 旅行计划 原生版 安装程序（NSIS 3，0.14.2）。由 tools\win\package.ps1 调用：
;   makensis /INPUTCHARSET UTF8 /DVERSION=x.y.z /DVERNUM=x.y.z /DSTAGE=<收集好的程序目录> /DOUTFILE=<安装包> /DFILES=<files.nsh> TravelPlanner.nsi
; 规则（工作规则三、五）：默认装到 D:\TravelPlannerNative\（没有 D 盘时装到 %LOCALAPPDATA%\Programs\TravelPlannerNative）；
; 只为当前用户装（不要管理员）：开始菜单快捷方式、控制面板卸载项都在当前用户下；不加桌面快捷方式、不加开机启动。
; 卸载和覆盖安装只删程序自己的文件（files.nsh 按收集目录的顶层文件 / 文件夹生成），
; 不删 data\（旅行数据）、settings.json（设置）；cache\（缩略图、地图瓦片，能重新生成）覆盖安装时保留、卸载时删掉。
; 日志在 %LOCALAPPDATA%\TravelPlannerNative\logs，卸载也不删。

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define APPKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TravelPlannerNative"
!define EXE "TravelPlanner.exe"

Name "$(AppName) ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "D:\TravelPlannerNative"
InstallDirRegKey HKCU "${APPKEY}" "InstallLocation"
BrandingText "TravelPlanner ${VERSION}"

VIProductVersion "${VERNUM}.0"
VIAddVersionKey /LANG=0 "ProductName" "TravelPlanner"
VIAddVersionKey /LANG=0 "FileDescription" "TravelPlanner ${VERSION} Setup"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "LegalCopyright" "Jason Yan"

!define MUI_ICON "..\assets\app\icon.ico"
!define MUI_UNICON "..\assets\app\icon.ico"
!define MUI_ABORTWARNING

!define MUI_DIRECTORYPAGE_TEXT_TOP "$(DirTop)"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!insertmacro MUI_PAGE_FINISH

!define MUI_UNCONFIRMPAGE_TEXT_TOP "$(UnTop)"
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; 先放中文：系统界面是中文时用中文，其他语言用英文（NSIS 按系统界面语言自动选，不弹选择框）
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

LangString AppName ${LANG_SIMPCHINESE} "旅行计划"
LangString AppName ${LANG_ENGLISH} "Travel Planner"
LangString DirTop ${LANG_SIMPCHINESE} "选择安装位置。你的旅行数据会放在这个文件夹的 data 里；以后覆盖安装、卸载都不会删它。"
LangString DirTop ${LANG_ENGLISH} "Choose where to install. Your travel data is kept in the data folder inside it; reinstalling or uninstalling never deletes it."
LangString UnTop ${LANG_SIMPCHINESE} "将删除程序文件和缓存。data 文件夹（你的旅行数据）和 settings.json（设置）会保留，不需要时可以自己删。"
LangString UnTop ${LANG_ENGLISH} "Program files and the cache will be removed. The data folder (your travel data) and settings.json are kept; delete them yourself if you no longer need them."
LangString Running ${LANG_SIMPCHINESE} "旅行计划正在运行。请先退出它（包括托盘里的图标），再点“重试”。"
LangString Running ${LANG_ENGLISH} "Travel Planner is running. Please quit it (including the tray icon), then click Retry."

!include "${FILES}"   ; RemoveProgramFiles：删上一版 / 这一版的程序文件

; 程序在运行时 exe 打不开写入：提示先退出（静默模式直接放弃，返回非 0）
!macro WaitNotRunning
  IfFileExists "$INSTDIR\${EXE}" 0 +7
  ClearErrors
  FileOpen $0 "$INSTDIR\${EXE}" a
  IfErrors 0 +3
  MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(Running)" /SD IDCANCEL IDRETRY -4
  Abort
  FileClose $0
!macroend

Function .onInit
  ; 没有 D 盘（或上次没装过）时换到当前用户的程序文件夹
  ReadRegStr $0 HKCU "${APPKEY}" "InstallLocation"
  StrCmp $0 "" 0 done
  IfFileExists "D:\*.*" done
  StrCpy $INSTDIR "$LOCALAPPDATA\Programs\TravelPlannerNative"
  done:
FunctionEnd

Section "Install"
  SetShellVarContext current
  !insertmacro WaitNotRunning
  !insertmacro RemoveProgramFiles
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\$(AppName).lnk" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${APPKEY}" "DisplayName" "$(AppName)"
  WriteRegStr HKCU "${APPKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${APPKEY}" "Publisher" "Jason Yan"
  WriteRegStr HKCU "${APPKEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${APPKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${APPKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${APPKEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${APPKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${APPKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${APPKEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  !insertmacro WaitNotRunning
  !insertmacro RemoveProgramFiles
  RMDir /r "$INSTDIR\cache"
  Delete "$SMPROGRAMS\旅行计划.lnk"
  Delete "$SMPROGRAMS\Travel Planner.lnk"
  DeleteRegKey HKCU "${APPKEY}"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"   ; 只在空的时候删（data\ 还在就留着）
SectionEnd
