param([string]$Evidence)
$ErrorActionPreference='Stop'
$repo=Join-Path $Evidence 'repo'
$fixture=Join-Path $Evidence 'scope-fixture'
& git clone --shared --quiet --no-checkout $repo $fixture
if($LASTEXITCODE){throw 'scope fixture clone failed'}
& git -C $fixture config core.longpaths true
& git -C $fixture sparse-checkout init --cone
& git -C $fixture sparse-checkout set 'RobWork/RobWork/src' 'RobWork/RobWorkSim/src' 'RobWork/RobWork/gtest' 'RobWork/RobWorkSim/gtest' 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot' 'RobWork/doc/industrial-robot-design/reuse' 'RobWork/doc/industrial-robot-design/tasks' 'RobWork/doc/industrial-robot-design/traceability/acceptance'
& git -C $fixture checkout --quiet --detach HEAD
if($LASTEXITCODE){throw 'scope fixture checkout failed'}
$catalog=Join-Path $fixture 'RobWork/doc/industrial-robot-design/reuse/catalog.json'
$original=[IO.File]::ReadAllBytes($catalog)
function Digest([string]$s){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($s.TrimStart([char]0xFEFF) -replace "`r`n","`n").TrimEnd("`n")+"`n")))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
$cases=@(
    @{name='rw-real-gtest-root';upstream='RobWork';target='sdurw-gtest-main';path='RobWork/RobWork/gtest/CMakeLists.txt';expected='PASS'},
    @{name='sim-real-gtest-root';upstream='RobWorkSim';target='sdurwsim-gtest-main';path='RobWork/RobWorkSim/gtest/CMakeLists.txt';expected='PASS'},
    @{name='rw-nested-gtest-rejected';upstream='RobWork';target='sdurw-gtest-main';path='RobWork/RobWork/gtest/fake/CMakeLists.txt';expected='REUSE-UPSTREAM-TARGET'},
    @{name='sim-nested-gtest-rejected';upstream='RobWorkSim';target='sdurwsim-gtest-main';path='RobWork/RobWorkSim/gtest/fake/CMakeLists.txt';expected='REUSE-UPSTREAM-TARGET'},
    @{name='gtest-suffix-dir-rejected';upstream='RobWork';target='sdurw-gtest-main';path='RobWork/RobWork/gtest2/CMakeLists.txt';expected='REUSE-UPSTREAM-TARGET'},
    @{name='studio-gtest-root-rejected';upstream='RobWork';target='sdurw-gtest-main';path='RobWork/RobWorkStudio/gtest/CMakeLists.txt';expected='REUSE-UPSTREAM-TARGET'},
    @{name='cmake-suffix-rejected';upstream='RobWork';target='sdurw-gtest-main';path='RobWork/RobWork/gtest/CMakeLists.txt.fake';expected='REUSE-UPSTREAM-TARGET'},
    @{name='real-root-forged-target-rejected';upstream='RobWork';target='NoSuchTargetScopeAudit';path='RobWork/RobWork/gtest/CMakeLists.txt';expected='REUSE-UPSTREAM-TARGET'}
)
$results=@()
foreach($c in $cases){
    $j=([Text.Encoding]::UTF8.GetString($original).TrimStart([char]0xFEFF)|ConvertFrom-Json)
    $u=$j.capabilities[0].upstream[0]
    $u.path='RobWork/'+$c.upstream+'/gtest/test-main.cpp';$u.symbols=@('main')
    $u.sha256=Digest ([IO.File]::ReadAllText((Join-Path $fixture $u.path)))
    $u.target=$c.target;$u.targetFile=$c.path
    $targetFile=Join-Path $fixture $u.targetFile
    if(-not(Test-Path -LiteralPath $targetFile)){[IO.Directory]::CreateDirectory((Split-Path -Parent $targetFile))|Out-Null;[IO.File]::WriteAllText($targetFile,('add_library('+$c.target+' STATIC fake.cpp)'))}
    $u.targetSha256=Digest ([IO.File]::ReadAllText($targetFile))
    $j.capabilities[0].build.targets=@($j.capabilities[0].upstream|ForEach-Object{$_.target}|Sort-Object -Unique)
    [IO.File]::WriteAllText($catalog,($j|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
    $saved=$ErrorActionPreference
    try{$ErrorActionPreference='Continue';$output=(& pwsh -NoProfile -File (Join-Path $repo 'RobWork/scripts/industrialrobot/validate-reuse.ps1') -RepoRoot $fixture 2>&1|ForEach-Object{"$_"}|Out-String -Width 32767);$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
    [IO.File]::WriteAllText((Join-Path $Evidence ('scope-'+$c.name+'.log')),$output)
    $ok=($c.expected -eq 'PASS' -and $code -eq 0)-or($c.expected -ne 'PASS' -and $code -ne 0 -and $output.Contains($c.expected))
    $results+=[pscustomobject]@{name=$c.name;targetFile=$c.path;expected=$c.expected;exitCode=$code;passed=$ok;output=$output}
    [IO.File]::WriteAllBytes($catalog,$original)
    Write-Output ($c.name+': '+$ok)
}
$results|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Evidence 'scope-probes.json') -Encoding utf8
if(@($results|Where-Object{-not $_.passed}).Count){throw 'scope probes failed'}
