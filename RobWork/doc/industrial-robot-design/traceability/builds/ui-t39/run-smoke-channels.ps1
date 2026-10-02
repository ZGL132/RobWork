# UI-T39 留痕三冒烟通道驱动（layout 双场景＋auto＋requirements-tour）。
# 基于 ui-t37 驱动 v2 形态；净室夹具扩展＝三区可见性键一并清除（本批底部
# 区出厂隐藏，且呼出/复位断言会写 layout/visibleBottom——场景 1 必须无记忆）。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build"
$OUT = "$ROOT\RobWork\doc\industrial-robot-design\traceability\builds\ui-t39\smoke"
$CWD = "$ROOT\build\ui-t39-smoke-cwd"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

$iniPath = "$env:APPDATA\sdurws\ird-workbench.ini"
function Reset-CleanFixture {
    if (Test-Path $iniPath) {
        # QSettings IniFormat：layout/visibleBottom 写作 [layout] 组（组头＋
        # 组内键行），aux.domain.* 是 [general] 组内点号键行——两类都要清
        # （净室夹具；残留的 visibleBottom 记忆会压过工厂缺省——PM-14 记忆
        # 优先是产品语义，夹具必须真实删除整组）。
        $kept = New-Object System.Collections.Generic.List[string]
        $inLayout = $false
        foreach ($line in (Get-Content $iniPath -Encoding UTF8)) {
            if ($line -match '^\[layout\]') { $inLayout = $true; continue }
            if ($line -match '^\[') { $inLayout = $false }
            if ($inLayout) { continue }
            if ($line -match '^aux\.domain\.') { continue }
            $kept.Add($line)
        }
        Set-Content $iniPath -Value $kept -Encoding UTF8
        Write-Host "fixture-reset: ini lines kept=$($kept.Count) (layout group + aux.domain keys removed)"
    } else {
        Write-Host "fixture-reset: no ini present (clean first-start)"
    }
}

# F-455 口径：PATH 需求清单＝主树各 bin＋vcpkg＋Qt＋Miniconda。
$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "UI-T38 smoke channels @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

function Run-Smoke([string]$mode, [string]$consoleLog, [switch]$CleanFixture) {
    if ($CleanFixture) {
        Reset-CleanFixture
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
    Add-Content $consoleLog (Get-Content $e -Raw -ErrorAction SilentlyContinue)
    Write-Host "run($mode) exit=$($proc.ExitCode)"
    return $proc.ExitCode
}

# 通道一/二：layout 双场景（净室→记忆优先）；净室夹具只在场景 1 前。
$c1 = Run-Smoke 'layout' "$OUT\console-layout.log" -CleanFixture
$c2 = Run-Smoke 'layout2' "$OUT\console-layout2.log"
# 通道三：auto 集成冒烟（净室夹具——auto 含默认呈现断言）。
$c3 = Run-Smoke 'auto' "$OUT\console-auto.log" -CleanFixture
# 通道四：requirements-tour 全功能遍历（净室夹具——新建项目起步）。
$c4 = Run-Smoke 'requirements-tour' "$OUT\console-requirements-tour.log" -CleanFixture
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue

foreach ($f in @('console-layout.log','console-layout2.log','console-auto.log','console-requirements-tour.log')) {
    Write-Output "---- $f ----"
    Get-Content "$OUT\$f" -ErrorAction SilentlyContinue |
        Select-String -Pattern 'pass |FAIL|DONE|started|TOUR|SMOKE' |
        Select-Object -First 12 | ForEach-Object { $_.Line }
}

if ($c1 -eq 0 -and $c2 -eq 0 -and $c3 -eq 0 -and $c4 -eq 0) { Write-Output 'UI_T39_SMOKE_ALL_PASS' } else { Write-Output "UI_T39_SMOKE_FAIL c1=$c1 c2=$c2 c3=$c3 c4=$c4" }
