$ErrorActionPreference='Stop'
$out=$PSScriptRoot;$repo=Join-Path $out 'repo'
& 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> (Join-Path $out 'vs-environment.log')
$env:QT_QPA_PLATFORM='windows'
$env:PATH='D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;'+$env:PATH
Get-ChildItem Env:|Where-Object{$_.Name-match'^(QT_|QML_|VSCMD|VCTools)'}|Select-Object Name,Value|ConvertTo-Json|Set-Content (Join-Path $out 'inherited-runtime-environment.json')
$exe='C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/.cache/b22-execution/Release/ird_execution_boundary_audit.exe'
$scripts=Join-Path $repo 'RobWork/scripts/industrialrobot'
Copy-Item -LiteralPath (Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch23/flake-before.ps1') -Destination (Join-Path $scripts 'flake-before-replay.ps1')
$results=@()
foreach($version in @('before','after')){
 $script=if($version-eq'before'){Join-Path $scripts 'flake-before-replay.ps1'}else{Join-Path $scripts 'run-flake-probe.ps1'}
 foreach($c in @(@{name='zero';filter='ReuseNoSuchTestSuite.*';expected=([int]($version-eq'after'))},@{name='healthy';filter='RunRegistryAdmissionTest.RegisterRejectsCallerContractViolations';expected=0})){
  $dir=Join-Path $out ("process-$version-"+$c.name);New-Item -ItemType Directory -Path $dir|Out-Null
  Push-Location $dir
  try{& pwsh -NoProfile -File $script -TestExe $exe -Repeat 1 -OutDir $dir -GtestFilter $c.filter *> (Join-Path $dir 'driver.log');$code=$LASTEXITCODE}finally{Pop-Location}
  [IO.File]::WriteAllText((Join-Path $dir 'exit.txt'),[string]$code)
  $results+=[pscustomobject]@{version=$version;case=$c.name;exitCode=$code;expected=$c.expected;passed=($code-eq$c.expected);exe=$exe;scriptSha256=(Get-FileHash $script).Hash}
 }
}
$results|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'process-results.json') -Encoding UTF8
$results|Format-Table version,case,exitCode,expected,passed
if(@($results|Where-Object{-not$_.passed}).Count){exit 1}
