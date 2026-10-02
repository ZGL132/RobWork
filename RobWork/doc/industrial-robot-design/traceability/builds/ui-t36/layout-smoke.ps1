# UI-T27 留痕 layout 冒烟双场景驱动（基于 UI-T25/T26 驱动 v2 形态；exe 与
# 依赖路径指向验收冷启树 build/acc-build-ui-t27；UI-T27 返工补拍——三域面板
# 呼出截图随场景 1 落盘）
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build\acc-build-ui-t27"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t36\smoke"
$CWD = "$ROOT\build\ui-t35-smoke-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

$iniPath = "$env:APPDATA\sdurws\ird-workbench.ini"
function Reset-AuxFixture {
    if (Test-Path $iniPath) {
        $lines = Get-Content $iniPath -Encoding UTF8
        $kept = $lines | Where-Object { $_ -notmatch '^aux\.domain\.' }
        Set-Content $iniPath -Value $kept -Encoding UTF8
        Write-Host "fixture-reset: aux.domain.* keys removed=$($lines.Count - $kept.Count)"
    } else {
        Write-Host "fixture-reset: no ini present (clean first-start)"
    }
}

$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "UI-T31 layout smoke @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

function Run-Smoke([string]$mode, [string]$consoleLog, [switch]$CleanFixture) {
    # 函数内进度行一律 Write-Host（Write-Output 会混入返回值——首版教训）。
    if ($CleanFixture) {
        Reset-AuxFixture
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

# 净室夹具只在场景 1 前执行（场景 1 呼出需求面板写记忆→场景 2 验证记忆优先）。
$c1 = Run-Smoke 'layout' "$OUT\console-layout.log" -CleanFixture
$c2 = Run-Smoke 'layout2' "$OUT\console-layout2.log"
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue

Get-Content "$OUT\console-layout.log", "$OUT\console-layout2.log" |
    Select-String -Pattern 'pass |FAIL|snap |DONE|started' |
    ForEach-Object { $_.Line }

if ($c1 -eq 0 -and $c2 -eq 0) { Write-Output 'LAYOUT_SMOKE_PASS' } else { Write-Output 'LAYOUT_SMOKE_FAIL' }
