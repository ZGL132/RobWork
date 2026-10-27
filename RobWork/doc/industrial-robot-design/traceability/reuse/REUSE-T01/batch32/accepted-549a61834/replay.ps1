$ErrorActionPreference='Stop'
$root=Join-Path $PSScriptRoot 'repo'
Set-Location $root
$b='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch32'
$c=Get-Content "$b/commands.json" -Raw|ConvertFrom-Json
. $c.developerEnvironment[0] -Arch amd64 -HostArch amd64 -SkipAutomaticLocation *> "$PSScriptRoot/vs-env.log"
$env:QT_QPA_PLATFORM='windows'
$env:PATH=($c.pathPrefix -join ';')+';'+$env:PATH
$cfg=@($c.configuration)[1..($c.configuration.Count-1)]
$cfg[3]=Join-Path $PSScriptRoot 'build'
& cmake @cfg *> "$PSScriptRoot/configure.log"
$LASTEXITCODE|Set-Content "$PSScriptRoot/configure.exit.txt"
if($LASTEXITCODE -ne 0){exit 2}
& cmake --build "$PSScriptRoot/build" --config Release --target ird_event_lifetime_audit *> "$PSScriptRoot/build.log"
$LASTEXITCODE|Set-Content "$PSScriptRoot/build.exit.txt"
if($LASTEXITCODE -ne 0){exit 3}
New-Item -ItemType Directory -Path "$PSScriptRoot/run" -Force|Out-Null
& "$PSScriptRoot/build/Release/ird_event_lifetime_audit.exe" "--gtest_output=xml:$PSScriptRoot/run/gtest.xml" "--ird_report=$PSScriptRoot/run/ird-test-report.json" *> "$PSScriptRoot/run/stdout.log"
$LASTEXITCODE|Set-Content "$PSScriptRoot/run/exit.txt"
Get-Content "$PSScriptRoot/run/stdout.log" -Tail 12
