# RT-T16 可复现编排：每次调用只做一个步骤，日志与退出码按阶段独立保留。
param(
    [ValidateSet('integration','smoke')][string]$Mode = 'integration',
    [ValidateSet('configure','build','test','gates')][string]$Action = 'configure',
    [string]$Phase = 'baseline',
    [string]$Filter = '*',
    [string[]]$Targets = @('sdurws_ird_runtime_test')
)
$ErrorActionPreference = 'Stop'
$repoRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$buildDir = Join-Path $repoRoot $(if ($Mode -eq 'integration') { 'build' } else { 'build-smoke' })
$outDir = Join-Path $PSScriptRoot "$Mode-$Phase"
New-Item -ItemType Directory $outDir -Force | Out-Null
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$env:PATH = "D:/software/Qt/6.11.1/msvc2022_64/bin;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Miniconda3;$repoRoot/RobWork/RobWork/bin/Release;$repoRoot/RobWork/RobWorkSim/bin/Release;" + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
$cmake = 'D:/software/CMake/bin/cmake.exe'
$ctest = 'D:/software/CMake/bin/ctest.exe'
Push-Location $repoRoot
try {
    if ($Action -eq 'configure') {
        $sourceDir = if ($Mode -eq 'integration') { "$repoRoot/RobWork" } else { "$repoRoot/RobWork/RobWorkStudio/src/rwslibs/industrialrobot" }
        $argsForCmake = @('-S', $sourceDir, '-B', $buildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
            '-DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake',
            '-DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64',
            '-DPYTHON_EXECUTABLE=D:/software/Miniconda3/python.exe')
        if ($Mode -eq 'integration') { $argsForCmake += '-DRWS_BUILD_INDUSTRIALROBOT=ON' }
        & $cmake @argsForCmake *> "$outDir/configure.log"
    } elseif ($Action -eq 'build' -or $Action -eq 'gates') {
        # 强制保留数组：PowerShell 会把单元素管道结果解包成字符串，原生参数
        # splat 随后可能逐字符展开，导致 MSBuild 错找 s.vcxproj。
        [string[]]$actualTargets = @(if ($Action -eq 'gates') { 'ird_gates' } else { $Targets })
        $buildArgs = @('--build', $buildDir, '--config', 'Release', '--target') + $actualTargets + @('--parallel', '6')
        & $cmake @buildArgs *> "$outDir/$Action.log"
    } else {
        $testDir = if ($Mode -eq 'integration') { "$buildDir/RobWorkStudio/src/rwslibs/industrialrobot/runtime" } else { "$buildDir/runtime" }
        $env:GTEST_FILTER = $Filter
        $env:GTEST_OUTPUT = "xml:$outDir/runtime.xml"
        & $ctest --test-dir $testDir -C Release -R '^sdurws_ird_runtime_test$' -V --timeout 600 *> "$outDir/test.log"
        if (Test-Path "$testDir/ird-test-report.json") {
            Copy-Item "$testDir/ird-test-report.json" "$outDir/ird-test-report.json"
        }
    }
    $resultCode = $LASTEXITCODE
    Set-Content "$outDir/$Action.exit.txt" $resultCode -Encoding utf8
    Write-Output "$Mode / $Phase / $Action : exit=$resultCode ($outDir)"
    Get-Content "$outDir/$Action.log" -Tail 14
    exit $resultCode
} finally { Pop-Location }
