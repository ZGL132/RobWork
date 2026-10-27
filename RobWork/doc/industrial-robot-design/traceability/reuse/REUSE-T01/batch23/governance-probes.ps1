$ErrorActionPreference='Stop'
$b23Root=(Get-Location).Path
$b23Out=Join-Path $b23Root '.cache/b23-governance'
[IO.Directory]::CreateDirectory($b23Out)|Out-Null
$b23Task=Join-Path $b23Root 'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'
$b23Scripts=Join-Path $b23Root 'RobWork/scripts/industrialrobot'
$b23Cases=@()
function Invoke-B23Probe($Name,$Arguments,$Expected,$Marker) {
  $log=Join-Path $b23Out ($Name+'.log')
  & pwsh -NoProfile -File @Arguments *> $log
  $code=$LASTEXITCODE
  $ok=($code -eq $Expected -and (Get-Content $log -Raw) -match $Marker)
  $script:b23Cases += [pscustomobject]@{name=$Name;exitCode=$code;expectedExit=$Expected;marker=$Marker;passed=$ok;arguments=$Arguments}
  Write-Output "$Name exit=$code passed=$ok"
}
Invoke-B23Probe 'scope-valid' @("$b23Scripts/verify-task-scope.ps1",'-TaskFile',$b23Task,'-Base','cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7','-Head','daf0dcd175fb76c956154fe0213f2d791dad8575') 0 'verify-task-scope: PASS'
Invoke-B23Probe 'scope-invalid-sha' @("$b23Scripts/verify-task-scope.ps1",'-TaskFile',$b23Task,'-Base','B23_NO_SUCH_COMMIT','-Head','HEAD') 1 'cannot resolve base commit'
$bad=Get-Content $b23Task -Raw|ConvertFrom-Json
$bad.forbiddenFiles+= 'RobWork/doc/industrial-robot-design/reuse/catalog.json'
$badFile=Join-Path $b23Out 'forbidden-task.json'
$bad|ConvertTo-Json -Depth 50|Set-Content $badFile -Encoding utf8
Invoke-B23Probe 'scope-forbidden' @("$b23Scripts/verify-task-scope.ps1",'-TaskFile',$badFile,'-Base','cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7','-Head','daf0dcd175fb76c956154fe0213f2d791dad8575') 1 '\[forbidden\].*reuse/catalog.json'
Invoke-B23Probe 'b1-valid' @("$b23Scripts/validate-b1-batch.ps1") 0 'validate-b1-batch: PASS'
$batch=Get-Content 'RobWork/doc/industrial-robot-design/traceability/pipeline/b1-host-integration-batch.json' -Raw|ConvertFrom-Json
$batch.stages[0].order=9
$batchFile=Join-Path $b23Out 'invalid-batch.json'
$batch|ConvertTo-Json -Depth 50|Set-Content $batchFile -Encoding utf8
Invoke-B23Probe 'b1-order' @("$b23Scripts/validate-b1-batch.ps1",'-BatchFile',$batchFile) 1 'stage order must be contiguous'
$b23Cases|ConvertTo-Json -Depth 10|Set-Content (Join-Path $b23Out 'report.json') -Encoding utf8
if (@($b23Cases|Where-Object{-not $_.passed}).Count){exit 1}
$definitions=@()
foreach($file in @('verify-task-scope.ps1','validate-b1-batch.ps1','run-flake-probe.ps1')){
  $path='RobWork/scripts/industrialrobot/'+$file
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $b23Root $path),[ref]$null,[ref]$null)
  foreach($node in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)){
    $definitions+=[pscustomobject]@{path=$path;kind='function';symbol=$node.Name;startLine=$node.Extent.StartLineNumber;endLine=$node.Extent.EndLineNumber}
  }
  foreach($node in $ast.EndBlock.Statements){
    if($node -is [Management.Automation.Language.FunctionDefinitionAst]){continue}
    $definitions+=[pscustomobject]@{path=$path;kind='entry-statement';symbol=($node.Extent.Text -split "`n")[0].Trim();startLine=$node.Extent.StartLineNumber;endLine=$node.Extent.EndLineNumber}
  }
}
$definitions|ConvertTo-Json -Depth 5|Set-Content (Join-Path $b23Out 'definitions.json') -Encoding utf8
