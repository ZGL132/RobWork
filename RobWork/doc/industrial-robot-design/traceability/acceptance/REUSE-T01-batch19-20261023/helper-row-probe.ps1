param([Parameter(Mandatory=$true)][string]$FrozenRepo,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$original=$FrozenRepo
$RepoRoot=Join-Path ([IO.Path]::GetTempPath()) ('b19-row-'+[Guid]::NewGuid().ToString('N'))
$design='RobWork/doc/industrial-robot-design/';$Head='';$tree=@{};$units=@();$caps=@{};$RequireMigrationComplete=$false
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $original 'RobWork/scripts/industrialrobot/validate-reuse.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'frozen validator parse errors'}
$names=@('Fail','SafePath','ReadText','Digest','Json','GitLines')
foreach($f in $ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$true)|Where-Object Name -in $names){. ([scriptblock]::Create($f.Extent.Text))}
. (Join-Path $original 'RobWork/scripts/industrialrobot/reuse-contract.ps1')
$results=@()
try {
 $source='RobWork/scripts/industrialrobot/commit-lint.ps1'
 foreach($p in @($source,($design+'ARCHITECTURE.md'))){$dest=Join-Path $RepoRoot $p;[IO.Directory]::CreateDirectory((Split-Path $dest -Parent))|Out-Null;Copy-Item -LiteralPath (Join-Path $original $p) -Destination $dest}
 $text=ReadText $source
 $row=[pscustomobject]@{functionId='independent.governance.final-row';ownerUnit='foundation-governance';requirements=@('H6');sources=@([pscustomobject]@{path=$source;symbol='param';startLine=1;endLine=@(($text -replace "`r`n","`n") -split "`n").Count;sha256=(Digest $text)});targets=@();usageRole='owner';permissionRef='ARCH:3.6.4';capabilityId='NONE';decision='pending';status='pending';search=[pscustomobject]@{command='independent probe';conclusion='isolated row only'};differences='isolated final-row proof';inputs=@([pscustomobject]@{path=$source;sha256=(Digest $text)});execution=[pscustomobject]@{kind='governance-script';entry=$source;command=('pwsh -File '+$source)}}
 $path=Join-Path $RepoRoot ($design+'reuse/functions/foundation-governance.json');[IO.Directory]::CreateDirectory((Split-Path $path -Parent))|Out-Null
 foreach($kind in @('pending-positive','pending-final-rejected','closed-rejected')) {
  $row.status='pending';$RequireMigrationComplete=$kind -eq 'pending-final-rejected'
  if($kind -eq 'closed-rejected'){$row.status='closed'}
  [IO.File]::WriteAllText($path,([pscustomobject]@{schemaVersion='ird-reuse-functions/1';unit='foundation-governance';functions=@($row)}|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
  $message='';$caught=$false
  try{$message=(CheckFunctions @{} @()|Out-String)}catch{$message=$_.Exception.Message;$caught=$true}
  $expected='PASS';if($RequireMigrationComplete){$expected='REUSE-MIGRATION : unresolved function independent.governance.final-row'}elseif($kind -eq 'closed-rejected'){$expected='REUSE-EXECUTION'}
  $ok=(-not $caught -and $expected -eq 'PASS') -or ($caught -and $message.Contains($expected))
  $results += [pscustomobject]@{name=$kind;passed=$ok;expected=$expected;output=$message}
  [IO.File]::WriteAllText(($Output+'.'+$kind+'.log'),$message)
  Write-Output "$kind : $ok $message"
 }
 $r=[ordered]@{scope='actual frozen CheckFunctions + actual frozen dependency function definitions; isolated empty product baseline to reach exact pending-row final guard; not full validator run';expected=3;executed=$results.Count;passed=@($results|Where-Object passed).Count;failed=@($results|Where-Object {-not $_.passed}).Count;skipped=0;notRun=0;helperSha256=(Get-FileHash (Join-Path $original 'RobWork/scripts/industrialrobot/reuse-contract.ps1')).Hash;tests=$results}
 $r|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $Output -Encoding UTF8
 $xml=[xml]'<testsuite name="b19-helper-row" tests="3" failures="0" skipped="0"/>'
 $xml.DocumentElement.SetAttribute('failures',"$($r.failed)")
 foreach($t in $results){$n=$xml.CreateElement('testcase');$n.SetAttribute('name',$t.name);if(-not $t.passed){$f=$xml.CreateElement('failure');$f.InnerText=$t.output;[void]$n.AppendChild($f)};[void]$xml.DocumentElement.AppendChild($n)}
 $xml.Save(($Output+'.junit.xml'))
 if($r.failed){exit 1}
} finally {
 $tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 if([IO.Path]::GetFullPath($RepoRoot).StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $RepoRoot -Leaf) -like 'b19-row-*'){Remove-Item -LiteralPath $RepoRoot -Recurse -Force}
}
