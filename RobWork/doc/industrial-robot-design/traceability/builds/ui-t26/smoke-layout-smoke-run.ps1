# UI-T25 layout 冒烟双场景驱动 v2（Start-Process 句柄重定向捕获 GUI 程序 stdout）
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t26\smoke"
$CWD = "$ROOT\build\ui-t26-smoke-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

$iniPath = "$env:APPDATA\sdurws\ird-workbench.ini"
function Reset-AuxFixture {
    if (Test-Path $iniPath) {
        $lines = Get-Content $iniPath -Encoding UTF8
        $kept = $lines | Where-Object { $_ -notmatch '^aux\.domain\.' }
        Set-Content $iniPath -Value $kept -Encoding UTF8
        Write-Output "fixture-reset: aux.domain.* keys removed=$($lines.Count - $kept.Count)"
    } else {
        Write-Output "fixture-reset: no ini present (clean first-start)"
    }
}

$env:PATH = "$ROOT\vcpkg\installed\x64-windows\bin;$ROOT\build\RobWork\bin\Release;D:\software\Qt\6.11.1\msvc2022_64\bin;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$ROOT\build\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "UI-T25 layout smoke @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

function Run-Smoke([string]$mode, [string]$consoleLog, [switch]$CleanFixture) {
    # 注意：函数内进度行一律 Write-Host（Write-Output 会混入返回值——
    # 首版驱动 $c1 捕获到数组导致退出码比较恒假，LAYOUT_SMOKE 误报 FAIL）
    if ($CleanFixture) {
        Reset-AuxFixture | Write-Host
    }
    if ($mode -eq 'layout') {
        Remove-Item "$CWD\rwsettings.xml" -Force -ErrorAction SilentlyContinue
    }
    $env:IRD_UI_PLUGIN_SMOKE = $mode
    $o = "$CWD\console-$mode.out"; $e = "$CWD\console-$mode.err"
    Push-Location $CWD
    $proc = Start-Process -FilePath $exe -WorkingDirectory $CWD `
        -RedirectStandardOutput $o -RedirectStandardError $e `
        -PassThru -Wait
    Pop-Location
    Copy-Item $o $consoleLog -Force
    Write-Host "run($mode) exit=$($proc.ExitCode)"
    return $proc.ExitCode
}

# 净室夹具只在场景 1 前执行（场景 1 呼出需求面板写记忆→场景 2 验证记忆优先；
# 场景 2 前清夹具＝记忆被删——首版驱动的夹具时序缺陷，已修正）。
$c1 = Run-Smoke 'layout' "$OUT\console-layout.log" -CleanFixture
$c2 = Run-Smoke 'layout2' "$OUT\console-layout2.log"
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue

Get-Content "$OUT\console-layout.log", "$OUT\console-layout2.log" |
    Select-String -Pattern 'pass |FAIL|snap |DONE|started' |
    ForEach-Object { $_.Line }

if ($c1 -eq 0 -and $c2 -eq 0) { Write-Output 'LAYOUT_SMOKE_PASS' } else { Write-Output 'LAYOUT_SMOKE_FAIL' }
