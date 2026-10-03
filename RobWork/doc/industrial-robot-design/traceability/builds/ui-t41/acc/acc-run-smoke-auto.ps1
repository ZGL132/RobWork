# UI-T41 验收侧真宿主冒烟驱动（auto 通道——F-421 step7 断言复现）。
# 形态同 ui-t39 实施留痕 run-smoke-channels.ps1（净室夹具同款），仅两处
# 验收适配：$BLD 指向验收冷构建树 acc-ui-t41-1-build；输出目录指向验收
# evidence 暂存区。送验对象＝detached worktree @ 717e6f17 的冷树二进制。
$ErrorActionPreference = 'Continue'
$ROOT = 'D:\10_Source_Repos\21_robot\RobWork'
$BLD = "$ROOT\build\acc-ui-t41-1-build"
$OUT = "$ROOT\build\acc-ui-t41-1-evi\acc\acc-smoke"
$CWD = "$ROOT\build\acc-ui-t41-1-smoke-cwd-mut"
New-Item -ItemType Directory -Force -Path $OUT, $CWD | Out-Null

$iniPath = "$env:APPDATA\sdurws\ird-workbench.ini"
function Reset-CleanFixture {
    if (Test-Path $iniPath) {
        # 净室夹具：删除 [layout] 整组与 aux.domain.* 键（PM-14 记忆优先是
        # 产品语义——夹具必须真实删除，不能靠缺省覆盖）。
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
        Write-Host "fixture-reset: ini lines kept=$($kept.Count)"
    } else {
        Write-Host "fixture-reset: no ini present (clean first-start)"
    }
}

# F-459/F-455 口径：PATH＝冷树各 bin＋vcpkg＋Qt＋Miniconda。
$env:PATH = "$BLD\RobWork\bin\Release;$BLD\RobWorkSim\bin\Release;$BLD\RobWorkStudio\bin\Release;$BLD\RobWorkStudio\libs\Release;$ROOT\vcpkg\installed\x64-windows\bin;D:\software\Qt\6.11.1\msvc2022_64\bin;D:\software\Miniconda3;$env:PATH"
$env:IRD_UI_PLUGIN_SMOKE_OUT = $OUT
$exe = "$BLD\RobWorkStudio\bin\Release\sdurws_ird_studio.exe"

Write-Output "UI-T41 ACC smoke(auto) @ $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') exe=$exe"

Reset-CleanFixture
$env:IRD_UI_PLUGIN_SMOKE = 'auto'
$o = "$CWD\console-auto.out"; $e = "$CWD\console-auto.err"
Push-Location $CWD
$proc = Start-Process -FilePath $exe -WorkingDirectory $CWD `
    -RedirectStandardOutput $o -RedirectStandardError $e `
    -PassThru -Wait
Pop-Location
$consoleLog = "$OUT\console-auto.log"
Copy-Item $o $consoleLog -Force
Add-Content $consoleLog (Get-Content $e -Raw -ErrorAction SilentlyContinue)
Write-Output "run(auto) exit=$($proc.ExitCode)"
Remove-Item Env:IRD_UI_PLUGIN_SMOKE -ErrorAction SilentlyContinue

Write-Output "---- console-auto.log (smoke lines) ----"
Get-Content "$consoleLog" -ErrorAction SilentlyContinue |
    Select-String -Pattern 'step7|step1|DONE|FAIL|domain assembly' |
    ForEach-Object { $_.Line }

if ($proc.ExitCode -eq 0) { Write-Output 'ACC_SMOKE_AUTO_PASS' } else { Write-Output "ACC_SMOKE_AUTO_FAIL exit=$($proc.ExitCode)" }
