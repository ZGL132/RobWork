$b23Root=(Get-Location).Path; $b23Out=Join-Path $b23Root '.cache'
$definitions=@()
foreach($file in @('pipeline-lock.ps1','test-pipeline-pipeline.ps1')){
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
$definitions|ConvertTo-Json -Depth 5|Set-Content (Join-Path $b23Out 'b24-definitions.json') -Encoding utf8
