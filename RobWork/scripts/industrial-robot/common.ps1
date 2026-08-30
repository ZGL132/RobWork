# Shared helpers for industrial-robot PowerShell scripts (WP-01-T01).
# Scope limited to path resolution and logging per the task card; build/package
# data flow helpers arrive with WP-01-T02/T03.

function Get-IndustrialRobotRepoRoot {
    # Scripts live at <repo>\RobWork\scripts\industrial-robot; the repository root
    # is two levels up so that every script resolves paths identically.
    param([string]$ScriptRoot)
    return (Resolve-Path -LiteralPath (Join-Path $ScriptRoot '..\..')).Path
}

function New-IndustrialRobotLogDir {
    # Create out/logs/industrial-robot/<timestamp>/ and return its full path.
    # 运行日志与 out/test-evidence 同级，位于外层仓库根（内层 RobWork/out 被 .gitignore 忽略，
    # 证据必须可提交供独立验证复查）。
    param([string]$RepoRoot, [string]$TimeStamp)
    $evidenceRoot = Split-Path -Parent $RepoRoot
    $dir = Join-Path $evidenceRoot ("out\logs\industrial-robot\" + $TimeStamp)
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    return $dir
}

function Write-IndustrialRobotLog {
    param([string]$LogFile, [string]$Line)
    Add-Content -LiteralPath $LogFile -Value $Line -Encoding UTF8
}

# ---------------------------------------------------------------------------
# WP-01-T03 追加：参数解析、VS x64 环境、原生日志执行与 Qt 环境冲突检测。
# 以下函数为本任务卡新增；上方 WP-01-T01 既有函数及其签名保持不变。
# 契约：work-packages/WP-01 §5/§6；architecture/testing-contract.md §5。
# ---------------------------------------------------------------------------

function Get-IndustrialRobotTimeStamp {
    # 统一日志目录时间戳（yyyyMMdd-HHmmss），与既有证据目录命名一致。
    return (Get-Date -Format 'yyyyMMdd-HHmmss')
}

function Resolve-IndustrialRobotSourceDirectory {
    # 输入型目录解析：必须真实存在（WP-01 计划 §5.1 "参数路径先 Resolve-Path"）。
    # 不存在时不抛异常：输出路径诊断到 stderr 并返回 $null，由入口统一转非零退出码。
    param([string]$Path, [string]$Default, [string]$Label)
    $target = $Path
    if ([string]::IsNullOrWhiteSpace($target)) { $target = $Default }
    try {
        return (Resolve-Path -LiteralPath $target -ErrorAction Stop).Path
    }
    catch {
        [Console]::Error.WriteLine(('[入口参数] ' + $Label + ' 目录不存在或不可读: ' + $target))
        return $null
    }
}

function New-IndustrialRobotOutputDirectory {
    # 输出型目录解析与创建（WP-01 计划 §5.1 "输出父目录由脚本创建"）。
    # 创建失败时输出诊断并返回 $null；不删除任何既有目录。
    param([string]$Path, [string]$Default, [string]$Label)
    $target = $Path
    if ([string]::IsNullOrWhiteSpace($target)) { $target = $Default }
    try {
        $created = New-Item -ItemType Directory -Path $target -Force -ErrorAction Stop
        return $created.FullName
    }
    catch {
        [Console]::Error.WriteLine(('[入口参数] ' + $Label + ' 目录无法创建: ' + $target + '（' + $_.Exception.Message + '）'))
        return $null
    }
}

function Find-IndustrialRobotVsWhere {
    # WP-01 计划 §5.2：按顺序查找 vswhere.exe——先 PATH，后默认安装器位置；找不到返回 ''。
    $command = Get-Command -Name 'vswhere.exe' -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    return ''
}

function Invoke-IndustrialRobotNative {
    # 统一原生命令执行：stdout+stderr 逐行写入日志并回显控制台，返回退出码与输出行。
    # PS 5.1 在 $ErrorActionPreference='Stop' 下 2>&1 会把原生命令 stderr 行升级为终止错误，
    # 因此仅在捕获期间临时切换为 Continue，出口恢复原值。
    param([string]$FilePath, [string[]]$ArgumentList, [string]$LogFile, [string]$Label)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $lines = New-Object System.Collections.Generic.List[string]
    $code = -1
    try {
        $raw = & $FilePath @ArgumentList 2>&1
        $code = $LASTEXITCODE
        foreach ($item in @($raw)) { $lines.Add(('' + $item)) }
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($Label -ne '') {
        Write-IndustrialRobotLog -LogFile $LogFile -Line ('[命令] ' + $Label + ' → ' + $FilePath + ' ' + ($ArgumentList -join ' '))
    }
    foreach ($line in $lines) {
        if ($LogFile -ne '') { Write-IndustrialRobotLog -LogFile $LogFile -Line $line }
    }
    foreach ($line in $lines) { [Console]::Out.WriteLine($line) }
    return New-Object PSObject -Property @{ ExitCode = $code; Lines = $lines }
}

function Initialize-IndustrialRobotVsEnvironment {
    # WP-01 计划 §5.2 VS x64 环境初始化：
    #   1) 检查 cl.exe、cmake.exe、ctest.exe；
    #   2) 缺少 MSVC 环境时 vswhere → VsDevCmd.bat -arch=x64 -host_arch=x64，
    #      在本进程导入导出的 PATH/INCLUDE/LIB/VSINSTALLDIR 等变量；
    #   3) 记录 MSVC、Windows SDK、CMake、CTest 版本（写入 -LogFile）；
    #   4) 找不到 VS x64 立即失败（Success=$false），不回退 x86/MinGW。
    param([string]$LogFile)
    $result = New-Object PSObject -Property @{
        Success = $false; Via = ''; MsvcLine = ''; SdkVersion = ''
        CMakeLine = ''; CTestLine = ''; Diagnostic = ''
    }
    $cl = Get-Command -Name 'cl.exe' -ErrorAction SilentlyContinue
    $cmake = Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue
    $ctest = Get-Command -Name 'ctest.exe' -ErrorAction SilentlyContinue
    if ($null -eq $cl) {
        $vswhere = Find-IndustrialRobotVsWhere
        if ($vswhere -eq '') {
            $result.Diagnostic = 'VS x64 环境发现失败：PATH 与默认安装器位置均未找到 vswhere.exe；不回退 x86/MinGW。'
            return $result
        }
        $query = Invoke-IndustrialRobotNative -FilePath $vswhere `
            -ArgumentList @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath') `
            -LogFile $LogFile -Label 'vswhere 查询 x64 实例'
        $installation = $null
        foreach ($line in $query.Lines) {
            $trimmed = $line.Trim()
            if ($trimmed -ne '' -and (Test-Path -LiteralPath $trimmed)) { $installation = $trimmed; break }
        }
        if ($null -eq $installation) {
            $result.Diagnostic = 'VS x64 环境发现失败：vswhere 未发现含 x64 工具集的 Visual Studio 实例；不回退 x86/MinGW。'
            return $result
        }
        $vsDevCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
        if (-not (Test-Path -LiteralPath $vsDevCmd)) {
            $result.Diagnostic = 'VS x64 环境发现失败：缺少 VsDevCmd.bat: ' + $vsDevCmd + '；不回退 x86/MinGW。'
            return $result
        }
        $imported = Invoke-IndustrialRobotNative -FilePath $env:ComSpec `
            -ArgumentList @('/s', '/c', ('"' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && set')) `
            -LogFile $LogFile -Label 'VsDevCmd -arch=x64 -host_arch=x64 环境导入'
        if ($imported.ExitCode -ne 0) {
            $result.Diagnostic = 'VS x64 环境导入失败：VsDevCmd.bat 退出码 ' + $imported.ExitCode + '；不回退 x86/MinGW。'
            return $result
        }
        foreach ($line in $imported.Lines) {
            if ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') {
                if ($Matches[1] -ieq 'Path') { $env:Path = $Matches[2] }
                else { Set-Item -Path ('env:' + $Matches[1]) -Value $Matches[2] -ErrorAction SilentlyContinue }
            }
        }
        $result.Via = 'VsDevCmd(' + $installation + ')'
        $cl = Get-Command -Name 'cl.exe' -ErrorAction SilentlyContinue
        $cmake = Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue
        $ctest = Get-Command -Name 'ctest.exe' -ErrorAction SilentlyContinue
    }
    else {
        $result.Via = '继承环境（cl.exe 已在 PATH）'
    }
    if ($null -eq $cl) {
        $result.Diagnostic = 'VS x64 环境发现失败：环境导入后 cl.exe 仍不可用；不回退 x86/MinGW。'
        return $result
    }
    if ($null -eq $cmake -or $null -eq $ctest) {
        $result.Diagnostic = 'VS x64 环境发现失败：cmake.exe/ctest.exe 不可用。'
        return $result
    }
    $clProbe = Invoke-IndustrialRobotNative -FilePath $cl.Source -ArgumentList @() -LogFile $LogFile -Label 'MSVC 版本探测'
    # cl 横幅首行即版本行；中英文输出均无固定 "Version" 字样，取首条非空行以语言无关方式记录。
    foreach ($line in $clProbe.Lines) {
        if ($line.Trim() -ne '') { $result.MsvcLine = $line.Trim(); break }
    }
    if ($result.MsvcLine -eq '') { $result.MsvcLine = 'cl.exe 版本输出未识别' }
    $result.SdkVersion = ''
    if ($null -ne $env:LIB) {
        foreach ($dir in ($env:LIB -split ';')) {
            if ($dir -match '\\Windows Kits\\10\\Lib\\([^\\]+)\\') { $result.SdkVersion = $Matches[1]; break }
        }
    }
    if ($result.SdkVersion -eq '') { $result.SdkVersion = '未从 LIB 环境识别到 Windows Kits\10\Lib 版本条目' }
    $cmakeProbe = Invoke-IndustrialRobotNative -FilePath $cmake.Source -ArgumentList @('--version') -LogFile $LogFile -Label 'CMake 版本'
    if ($cmakeProbe.Lines.Count -gt 0) { $result.CMakeLine = $cmakeProbe.Lines[0] }
    $ctestProbe = Invoke-IndustrialRobotNative -FilePath $ctest.Source -ArgumentList @('--version') -LogFile $LogFile -Label 'CTest 版本'
    if ($ctestProbe.Lines.Count -gt 0) { $result.CTestLine = $ctestProbe.Lines[0] }
    $result.Success = $true
    return $result
}

function Get-IndustrialRobotQtEnvironmentConflicts {
    # GUI 规则前置检查（testing-contract §5、WP-01 计划 §6）：
    #   - QT_QPA_PLATFORM 必须未设置或等于 windows；
    #   - 其余任何继承的 QT_*/QML_* 变量一律视为冲突，由调用方"先报告并停止"。
    # 返回冲突清单（NAME=value 字符串数组）；调用方以 @() 包裹接收。
    $conflicts = @()
    foreach ($entry in (Get-ChildItem -Path 'env:' | Where-Object { $_.Name -match '^(QT|QML)_' })) {
        if ($entry.Name -ieq 'QT_QPA_PLATFORM') {
            if ($entry.Value -ine 'windows') { $conflicts += ('QT_QPA_PLATFORM=' + $entry.Value) }
        }
        else {
            $conflicts += ($entry.Name + '=' + $entry.Value)
        }
    }
    return $conflicts
}
