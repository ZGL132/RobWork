$ErrorActionPreference='Stop'
$repo=Join-Path $PSScriptRoot 'repo'
$base='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch21/'
$expected=Get-Content (Join-Path $repo ($base+'script-definitions.json')) -Raw -Encoding utf8|ConvertFrom-Json
$actual=@()
foreach($path in @('commit-lint.ps1','validate-task.ps1','verify-task.ps1','test-governance-lite.ps1')) {
  $rel='RobWork/scripts/industrialrobot/'+$path
  $tokens=$null;$errors=$null
  $ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $rel),[ref]$tokens,[ref]$errors)
  if($errors.Count) {throw "Parse errors: $rel"}
  foreach($fn in $ast.FindAll({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst]},$true)) {
    $actual += [pscustomobject]@{path=$rel;kind='function';symbol=$fn.Name;startLine=$fn.Extent.StartLineNumber;endLine=$fn.Extent.EndLineNumber}
  }
  foreach($statement in $ast.EndBlock.Statements) {
    if($statement -is [System.Management.Automation.Language.FunctionDefinitionAst]) {continue}
    $actual += [pscustomobject]@{path=$rel;kind='entry-statement';symbol=($statement.Extent.Text -split "`r?`n")[0];startLine=$statement.Extent.StartLineNumber;endLine=$statement.Extent.EndLineNumber}
  }
}
$actual|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot 'definitions-replay.json') -Encoding utf8
$actualLines=@($actual|ForEach-Object {($_.path,$_.kind,$_.symbol,$_.startLine,$_.endLine) -join '|'})
$expectedLines=@($expected|ForEach-Object {($_.path,$_.kind,$_.symbol,$_.startLine,$_.endLine) -join '|'})
$diff=@(Compare-Object $actualLines $expectedLines)
[pscustomobject]@{expected=$expected.Count;actual=$actual.Count;differences=$diff;matches=($diff.Count -eq 0)}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $PSScriptRoot 'definitions-checks.json') -Encoding utf8
if($diff.Count) {throw 'Definition mismatch'}
Write-Output "AST definitions match: $($actual.Count) entries"
