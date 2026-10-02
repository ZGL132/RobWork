# UI-T37 返工④留痕 layout 冒烟双场景驱动（基于 ui-t35 驱动 v2 形态；
# exe 与依赖路径指向主构建树 build/——返工④工况页单表整合＋详情卡后的
# 双场景回归：场景 1 净室默认布局，场景 2 面板记忆优先）。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t37\smoke-r4"
$CWD = "$ROOT\build\ui-t37-r4-smoke-cwd"
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

# F-455 口径：PATH 需求清单＝主树各 bin＋vcpkg＋Qt＋Miniconda（与冷树配方同族）。
$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "UI-T37-R4 layout smoke @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

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
