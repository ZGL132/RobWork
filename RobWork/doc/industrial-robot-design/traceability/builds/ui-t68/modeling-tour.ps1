# 建模域全功能遍历驱动（modeling-tour 通道——IRD_UI_PLUGIN_SMOKE=
# modeling-tour，UI-T67/F-496 取证）：新建项目→建模面板→模板重种子（模
# 态选单自动应答）→关节 Origin 编辑→结构新增→建模域应用→包导出，逐项
# 断言＋截图落盘 smoke-tour-modeling/；控制台报告随留痕归档。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t68\smoke-tour-modeling"
$CWD = "$ROOT\build\ui-t68-tour-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

Get-Process sdurws_ird_studio -ErrorAction SilentlyContinue | Stop-Process -Force

$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE = 'modeling-tour'
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "modeling-tour @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

$o = "$CWD\console-mtour.out"; $e = "$CWD\console-mtour.err"
Push-Location $CWD
$proc = Start-Process -FilePath $exe -WorkingDirectory $CWD `
    -RedirectStandardOutput $o -RedirectStandardError $e `
    -PassThru -Wait
Pop-Location
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue
Remove-Item Env:IRD_UI_PLUGIN_SMOKE_OUT -ErrorAction SilentlyContinue

Copy-Item $o "$OUT\console-mtour.log" -Force
Get-Content "$OUT\console-mtour.log" |
    Select-String -Pattern 'step |pass |FAIL|snap |DONE|started|exception' |
    ForEach-Object { $_.Line }

# 判定行随留痕归档（E-3/F-538③——ui-t67 验收实证缺口：exit/MTOUR_PASS
# 结论此前只产生于会话控制台，留痕目录内无字证）。断言摘录＋退出码＋判定
# 行入档 driver-verdict.log，留痕目录自含结论证据。
$verdict = if ($proc.ExitCode -eq 0) { 'MTOUR_PASS' } else { 'MTOUR_FAIL' }
@("===== driver verdict ($(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')) =====") +
    (Get-Content "$OUT\console-mtour.log" |
        Select-String -Pattern 'step |pass |FAIL|snap |DONE|started|exception' |
        ForEach-Object { $_.Line }) +
    @("exit=$($proc.ExitCode)", $verdict) |
    Set-Content -Path "$OUT\driver-verdict.log" -Encoding UTF8

Write-Output "exit=$($proc.ExitCode)"
if ($proc.ExitCode -eq 0) { Write-Output 'MTOUR_PASS' } else { Write-Output 'MTOUR_FAIL' }
