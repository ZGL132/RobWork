param([Parameter(Mandatory=$true)][string]$TaskFile,[string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path)
# RepoRoot＝git 仓库根（scripts/industrialrobot 上三级）：任务 JSON 的 verify 命令
# （pwsh -File RobWork/scripts/... 与 cmake --build build）均相对该根书写；
# 2026-09-10 修正：原 ..\..（<repo>/RobWork）导致所有 verify 命令路径解析失败。
& "$PSScriptRoot\validate-task.ps1" -TaskFile $TaskFile
$t = Get-Content $TaskFile -Raw | ConvertFrom-Json
foreach ($cmd in @($t.verify)) {
  Write-Output "> $cmd"
  Push-Location $RepoRoot
  try { Invoke-Expression $cmd; if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "verification failed: $cmd" } }
  finally { Pop-Location }
}
Write-Output "verify-task: PASS $($t.taskId)"\n
