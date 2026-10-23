# 只验证执行器接线：复用真实 validate-task，隔离重型复用扫描并计数。
# 门禁本身的语义由既有正负例负责；这里防止去重造成漏检或失败后继续执行。
param([string]$OutputDir = '.cache/lean-verification')
$ErrorActionPreference = 'Stop'
$repo = (git rev-parse --show-toplevel).Trim()
$out = [IO.Path]::GetFullPath((Join-Path $repo $OutputDir))
$scripts = Join-Path $out 'RobWork/scripts/industrialrobot'
$tasks = Join-Path $out 'RobWork/doc/industrial-robot-design/tasks'
New-Item -ItemType Directory -Force $scripts,$tasks | Out-Null
Copy-Item (Join-Path $repo 'RobWork/scripts/industrialrobot/verify-task.ps1') $scripts
Copy-Item (Join-Path $repo 'RobWork/scripts/industrialrobot/validate-task.ps1') $scripts
@'
param($RepoRoot,$TaskFile)
Add-Content (Join-Path $RepoRoot 'calls.txt') 'reuse'
$t = Get-Content $TaskFile -Raw | ConvertFrom-Json
if (-not $t.reuse -or $t.rejectReuse) { throw 'fixture reuse denied' }
$global:LASTEXITCODE = 0
'@ | Set-Content (Join-Path $scripts 'validate-reuse.ps1') -Encoding utf8
$results = @()
foreach ($case in @(
    @{name='reuse-once';reuse=$true;dry=$false;count=1;ok=$true;run=$true},
    @{name='dryrun-still-checks-once';reuse=$true;dry=$true;count=1;ok=$true;run=$false},
    @{name='legacy-task-not-exempt';reuse=$false;dry=$true;count=1;ok=$false;run=$false},
    @{name='reuse-denied-stops-command';reuse=$true;reject=$true;count=1;ok=$false;run=$false},
    @{name='invalid-contract-stops-command';reuse=$true;invalid=$true;count=0;ok=$false;run=$false}
)) {
    Remove-Item (Join-Path $out 'calls.txt'),(Join-Path $out 'ran.txt') -ErrorAction SilentlyContinue
    $t = @{taskId='TEST-T1';unit='runtime';masterWp='WP-A';title='fixture';status='planned';
        dependsOn=@();requirements=@();designRefs=@();verify=@("Set-Content '$out/ran.txt' ran")}
    if ($case.reuse) { $t.reuse = @{policy='H1-H6'} }
    if ($case.reject) { $t.rejectReuse = $true }
    if ($case.invalid) { $t.masterWp = 'bad' }
    $taskPath = Join-Path $tasks 'TEST-T1.json'
    $t | ConvertTo-Json -Depth 5 | Set-Content $taskPath -Encoding utf8
    $args = @('-NoProfile','-File',(Join-Path $scripts 'verify-task.ps1'),'-TaskFile',$taskPath,'-RepoRoot',$out)
    if ($case.dry) { $args += '-DryRun' }
    & pwsh @args *> (Join-Path $out ($case.name+'.log'))
    $code = $LASTEXITCODE
    $calls = @(Get-Content (Join-Path $out 'calls.txt') -ErrorAction SilentlyContinue).Count
    $ran = Test-Path (Join-Path $out 'ran.txt')
    $pass = (($code -eq 0) -eq $case.ok) -and ($calls -eq $case.count) -and ($ran -eq $case.run)
    $results += @{name=$case.name;pass=$pass;exitCode=$code;reuseCalls=$calls;commandRan=$ran}
}
$results | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out 'results.json') -Encoding utf8
$failed = @($results | Where-Object { -not $_.pass }).Count
Write-Output "preflight: passed=$($results.Count-$failed) failed=$failed skipped=0"
if ($failed) { exit 1 }
