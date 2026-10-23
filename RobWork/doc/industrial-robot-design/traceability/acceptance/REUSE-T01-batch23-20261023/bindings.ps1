$ErrorActionPreference='Stop'
$out=$PSScriptRoot;$repo=Join-Path $out 'repo';Set-Location $repo
function HashText([string]$s){$b=[Text.Encoding]::UTF8.GetBytes(($s.TrimStart([char]0xFEFF)-replace "`r`n","`n").TrimEnd("`n")+"`n");$h=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($h.ComputeHash($b))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}}
function RawHash($p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
$j=Get-Content 'RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json' -Raw|ConvertFrom-Json
$rows=@($j.functions|Where-Object {$_.auditBoundary -like '3脚本18*'})
$bindings=@();$astAll=@()
foreach($r in $rows){
 foreach($s in $r.sources){$txt=[IO.File]::ReadAllText((Join-Path $repo $s.path));$lines=@(($txt -replace "`r`n","`n")-split "`n");$reg=$lines[($s.startLine-1)..($s.endLine-1)]-join "`n";$bindings+=[pscustomobject]@{id=$r.functionId;kind='source';path=$s.path;start=$s.startLine;end=$s.endLine;hashOK=((HashText $reg)-eq $s.sha256);literalOK=$reg.Contains($s.symbol)}}
 foreach($b in @($r.inputs)+@($r.auditEvidence)){$bindings+=[pscustomobject]@{id=$r.functionId;kind='input-or-evidence';path=$b.path;hashOK=((HashText ([IO.File]::ReadAllText((Join-Path $repo $b.path))))-eq $b.sha256)}}
 if($r.status-ne'pending'-or$r.decision-ne'pending'-or@($r.targets).Count-ne 0-or$r.execution.kind-ne'governance-script'){throw "boundary $($r.functionId)"}
}
$bindings|ConvertTo-Json -Depth 7|Set-Content (Join-Path $out 'bindings.json') -Encoding UTF8
foreach($f in @('verify-task-scope.ps1','validate-b1-batch.ps1','run-flake-probe.ps1')){
 $p="RobWork/scripts/industrialrobot/$f";$errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo $p),[ref]$tokens,[ref]$errors)
 if($errors.Count){throw 'AST errors'}
 foreach($n in $ast.FindAll({param($n)$n-is[Management.Automation.Language.FunctionDefinitionAst]},$true)){$astAll+=[pscustomobject]@{path=$p;kind='function';symbol=$n.Name;startLine=$n.Extent.StartLineNumber;endLine=$n.Extent.EndLineNumber}}
 foreach($n in $ast.EndBlock.Statements){if($n-is[Management.Automation.Language.FunctionDefinitionAst]){continue};$astAll+=[pscustomobject]@{path=$p;kind='entry-statement';symbol=($n.Extent.Text-split"`n")[0].Trim();startLine=$n.Extent.StartLineNumber;endLine=$n.Extent.EndLineNumber}}
}
$astAll|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'definitions.json') -Encoding UTF8
$oldAst=Get-Content 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch23/governance/definitions.json' -Raw|ConvertFrom-Json
$astEqual=(@(Compare-Object ($oldAst|ConvertTo-Json -Depth 6) ($astAll|ConvertTo-Json -Depth 6)).Count-eq 0)
$archive=Join-Path $repo 'RobWork/doc/industrial-robot-design/traceability/acceptance/REUSE-T01-batch21-20261023'
$original='C:/Users/zgl18/AppData/Local/Temp/ird-b21-accept-35dfa8d161'
$manifest=Get-Content (Join-Path $archive 'artifact-manifest.json') -Raw|ConvertFrom-Json
$raw=@();foreach($m in $manifest){$p=Join-Path $archive $m.path;$o=Join-Path $original $m.path;$raw+=[pscustomobject]@{path=$m.path;bytes=(Get-Item -LiteralPath $p).Length;manifestOK=((RawHash $p)-eq$m.rawSha256-and(Get-Item -LiteralPath $p).Length-eq$m.bytes);originalExists=(Test-Path -LiteralPath $o);originalEqual=((Test-Path -LiteralPath $o)-and(RawHash $p)-eq(RawHash $o))}}
$raw|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'b21-original-byte-checks.json') -Encoding UTF8
$before='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch23/flake-before.ps1'
$psi=[Diagnostics.ProcessStartInfo]::new('git','show daf0dcd175fb76c956154fe0213f2d791dad8575:RobWork/scripts/industrialrobot/run-flake-probe.ps1');$psi.WorkingDirectory=$repo;$psi.UseShellExecute=$false;$psi.RedirectStandardOutput=$true;$p=[Diagnostics.Process]::Start($psi);$ms=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($ms);$p.WaitForExit();[IO.File]::WriteAllBytes((Join-Path $out 'parent-original.ps1'),$ms.ToArray())
$beforeText=[IO.File]::ReadAllText((Join-Path $repo $before));$parentText=[IO.File]::ReadAllText((Join-Path $out 'parent-original.ps1'))
$summary=[pscustomobject]@{rows=$rows.Count;bindings=$bindings.Count;bindingsFailed=@($bindings|Where-Object{-not$_.hashOK-or($_.PSObject.Properties['literalOK']-and-not$_.literalOK)}).Count;astDefinitions=$astAll.Count;astEqual=$astEqual;manifestEntries=$raw.Count;manifestFailed=@($raw|Where-Object{-not$_.manifestOK}).Count;originalFailed=@($raw|Where-Object{-not$_.originalEqual}).Count;beforeRawHash=(RawHash $before);parentRawHash=(RawHash (Join-Path $out 'parent-original.ps1'));beforeCRLF=([regex]::Matches($beforeText,"`r`n").Count);parentCRLF=([regex]::Matches($parentText,"`r`n").Count);beforeParentNormalizedEqual=(($beforeText-replace"`r`n","`n")-ceq($parentText-replace"`r`n","`n"));afterRawHash=(RawHash 'RobWork/scripts/industrialrobot/run-flake-probe.ps1')}
$summary|ConvertTo-Json -Depth 6|Set-Content (Join-Path $out 'binding-summary.json') -Encoding UTF8
$summary|Format-List
if($summary.bindingsFailed-or-not$astEqual-or$summary.manifestFailed-or$summary.originalFailed-or-not$summary.beforeParentNormalizedEqual){exit 1}
