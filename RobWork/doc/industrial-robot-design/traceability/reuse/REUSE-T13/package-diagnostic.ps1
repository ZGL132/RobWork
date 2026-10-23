# REUSE-T13 精确范围诊断，非CI入口、非全量门禁替代。
# 原全量检查仍保留drivetrain继承FAIL；这里只复用未改动的原函数检查本包。
param()
$ErrorActionPreference='Stop'
$RepoRoot=(Get-Location).Path
$Head=''; $RequireMigrationComplete=$false
$baseline='b12e8503cc1c2a1acc040f506842a02e126b9515'
$product='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
$design='RobWork/doc/industrial-robot-design/'
$entry=Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/validate-reuse.ps1'
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($entry,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw '原门禁AST解析失败' }
foreach ($f in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false)) {
    Invoke-Expression $f.Extent.Text
}
. (Join-Path $RepoRoot 'RobWork/scripts/industrialrobot/reuse-contract.ps1')
$catalog=Json ($design+'reuse/catalog.json')
$units=@($catalog.units); $caps=@{}
foreach ($c in $catalog.capabilities) {$caps[$c.id]=$c}
$tree=@{}
foreach ($line in (GitLines @('ls-tree','-r',$baseline,'--',$product))) {
    if ($line -match '^\d+ blob ([0-9a-f]{40})\t(.+)$' -and (IsProduct $Matches[2])) { $tree[$Matches[2]]=$Matches[1] }
}
try {
    CheckCatalogV2 $catalog
    $grants=CheckPermissions
    $task=Json ($design+'tasks/REUSE-T13.json')
    CheckTask $task
    $manifest=Json $task.reuse.manifest
    foreach ($f in $manifest.files) {
        if ($f.path -notin $task.allowedFiles -or (Digest (ReadText $f.path)) -ne $f.sha256) { throw ('本包凭证不匹配：'+$f.path) }
    }
    CheckFunctions $grants @($manifest.files.path)
    Write-Output 'REUSE-T13 package diagnostic: PASS (原函数/本包范围；不是全量准入PASS，未独验)'
    exit 0
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
