# UI-T82 验证驱动：VS x64 环境；每次仅启动一个绝对路径 GUI 可执行文件。
param(
 [ValidateSet('integration','smoke')][string]$Mode='integration',
 [ValidateSet('build','gui','model','studio','gates','validate')][string]$Action='build',
 [string]$Phase='final',
 [string]$Scale='1',
 [string]$Filter='*',
 [string[]]$Targets=@('sdurws_ird_ui_gui_test','sdurws_ird_ui_test')
)
$ErrorActionPreference='Stop'
$repo=(& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$tree=Join-Path $repo $(if($Mode -eq 'integration'){'build'}else{'build-smoke'})
$out=Join-Path $PSScriptRoot "$Mode-$Phase"
New-Item -ItemType Directory -Path $out -Force | Out-Null
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:QT_QPA_PLATFORM='windows'
$env:QT_SCALE_FACTOR=$Scale
$env:PATH="D:/software/Qt/6.11.1/msvc2022_64/bin;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Miniconda3;$tree/RobWork/bin/Release;$tree/RobWorkSim/bin/Release;"+$env:PATH
$cmake='D:/software/CMake/bin/cmake.exe'
Push-Location $repo
try {
 if($Action -eq 'build' -or $Action -eq 'gates') {
   [string[]]$names=@(if($Action -eq 'gates'){'ird_gates'}else{$Targets})
   & $cmake --build $tree --config Release --target @names --parallel 6 *> "$out/$Action.log"
   $code=$LASTEXITCODE
 } elseif($Action -eq 'validate') {
   & pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/UI-T82.json -RepoRoot $repo *> "$out/validate-task.log"
   $code=$LASTEXITCODE
   & pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1 -RepoRoot (Join-Path $repo RobWork) *> "$out/validate-docs.log"
   if($LASTEXITCODE -ne 0){$code=$LASTEXITCODE}
 } else {
   $bin=if($Mode -eq 'integration'){Join-Path $tree 'RobWorkStudio/bin/Release'}else{Join-Path $tree 'ui/Release'}
   $name=if($Action -eq 'gui'){'sdurws_ird_ui_gui_test.exe'}elseif($Action -eq 'model'){'sdurws_ird_ui_test.exe'}else{'sdurws_ird_studio.exe'}
   $exe=Join-Path $bin $name
   if($Action -eq 'studio') {
     # 实机巡检临时使用用户设置，先逐字备份并在进程退出后恢复；不并行打开产品。
     if(Get-Process sdurws_ird_studio,RobWorkStudio -ErrorAction SilentlyContinue){throw '已有产品/框架 GUI 进程，请关闭后再运行单实例巡检'}
     $ini=Join-Path $env:APPDATA 'sdurws/ird-workbench.ini'
     $backup=Join-Path $out 'user-settings.private-backup'
     $hadIni=Test-Path -LiteralPath $ini
     if($hadIni){Copy-Item -LiteralPath $ini -Destination $backup}
     $env:IRD_UI_PLUGIN_SMOKE='dock-resize'
     $env:IRD_UI_SMOKE_CLEAN_SETTINGS='1'
     $env:IRD_UI_PLUGIN_SMOKE_OUT=$out
     try {
       # 净室首启：验证六域默认隐藏、标签栏首次呼出后的创建时序；退出逐字恢复原设置。
       if($hadIni){Remove-Item -LiteralPath $ini}
       Push-Location $out
       try {
         $process=Start-Process -FilePath $exe -ArgumentList @('--startup-mode','empty') -WorkingDirectory $out -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput "$out/studio.log" -RedirectStandardError "$out/studio.stderr.log"
         $code=$process.ExitCode
         Set-Content "$out/studio.process.exit.txt" $code -Encoding utf8
         # GUI 子系统未附控制台时仍以结构化断言报告为判据，禁止把空日志当通过。
         $report=Join-Path $out 'dock-resize-report.txt'
         if(!(Test-Path $report) -or (Get-Content $report -Raw) -match '\[FAIL\]'){$code=1}
       } finally {Pop-Location}
     } finally {
       if($hadIni){Copy-Item -LiteralPath $backup -Destination $ini -Force; Remove-Item -LiteralPath $backup}
       elseif(Test-Path -LiteralPath $ini){Remove-Item -LiteralPath $ini}
       Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue
       Remove-Item Env:IRD_UI_SMOKE_CLEAN_SETTINGS -ErrorAction SilentlyContinue
     }
   } else {
     Push-Location $out
     try { & $exe "--gtest_filter=$Filter" "--gtest_output=xml:$out/$Action.xml" "--ird_report=$out/ird-$Action-report.json" *> "$out/$Action.log"; $code=$LASTEXITCODE } finally {Pop-Location}
   }
 }
 Set-Content "$out/$Action.exit.txt" $code -Encoding utf8
 Write-Output "$Mode/$Phase/$Action exit=$code"
 if(Test-Path "$out/$Action.log"){Get-Content "$out/$Action.log" -Tail 16}
 exit $code
} finally {Pop-Location}
