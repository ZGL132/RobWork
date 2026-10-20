$ErrorActionPreference='Stop'
$batchSource='C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-completion-20261020-b3g'
$batchTarget='C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/doc/industrial-robot-design/traceability/builds/modeling-completion-20261020-b3g'
if(Test-Path -LiteralPath $batchTarget){throw '新批证据目录已存在，禁止覆盖冻结证据'}
New-Item -ItemType Directory -Path $batchTarget | Out-Null
Get-ChildItem -LiteralPath $batchSource -File | Where-Object {$_.Extension -in @('.log','.xml','.json','.ps1','.py','.md')} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $batchTarget
}
foreach($batchSub in @('gui','modeling-tour')){
    if(Test-Path -LiteralPath (Join-Path $batchSource $batchSub)){
        Copy-Item -LiteralPath (Join-Path $batchSource $batchSub) -Destination $batchTarget -Recurse
    }
}
Write-Output "已新增证据：$batchTarget"
