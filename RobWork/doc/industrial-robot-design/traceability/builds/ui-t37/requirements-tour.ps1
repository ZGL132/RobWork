# 需求界面全功能遍历驱动（requirements-tour 通道——IRD_UI_PLUGIN_SMOKE=
# requirements-tour）：新建项目→需求面板三页全操作→校验→应用→撤销重做，
# 逐项断言＋五帧截图落盘 smoke-tour/；控制台报告随留痕归档。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t37\smoke-tour"
$CWD = "$ROOT\build\ui-t37-tour-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

# 进程清扫（残留实例锁 exe＝LNK1104——历次实录）。
Get-Process sdurws_ird_studio -ErrorAction SilentlyContinue | Stop-Process -Force

$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE = 'requirements-tour'
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "requirements-tour @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

$o = "$CWD\console-tour.out"; $e = "$CWD\console-tour.err"
Push-Location $CWD
$proc = Start-Process -FilePath $exe -WorkingDirectory $CWD `
    -RedirectStandardOutput $o -RedirectStandardError $e `
    -PassThru -Wait
Pop-Location
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue
Remove-Item Env:IRD_UI_PLUGIN_SMOKE_OUT -ErrorAction SilentlyContinue

Copy-Item $o "$OUT\console-tour.log" -Force
Get-Content "$OUT\console-tour.log" |
    Select-String -Pattern 'step |pass |FAIL|snap |DONE|started|exception' |
    ForEach-Object { $_.Line }

Write-Output "exit=$($proc.ExitCode)"
if ($proc.ExitCode -eq 0) { Write-Output 'TOUR_PASS' } else { Write-Output 'TOUR_FAIL' }
