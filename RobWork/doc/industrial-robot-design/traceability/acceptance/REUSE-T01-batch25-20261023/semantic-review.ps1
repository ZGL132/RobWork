param([string]$Root)
$ErrorActionPreference='Stop'
$repo=Join-Path $Root 'repo';$sha='d0fcb96f62a863684848161f7b8ecd93ef0be302';$parent='e60d5d3c9a47a24010964295a935d42986a290e1'
function Blob([string]$rev,[string]$path){$pi=[Diagnostics.ProcessStartInfo]::new();$pi.FileName='git';$pi.WorkingDirectory=$repo;$pi.Arguments='cat-file blob '+$rev+':'+$path;$pi.UseShellExecute=$false;$pi.RedirectStandardOutput=$true;$p=[Diagnostics.Process]::Start($pi);$m=[IO.MemoryStream]::new();$p.StandardOutput.BaseStream.CopyTo($m);$p.WaitForExit();if($p.ExitCode){throw 'git blob failed'};return [Text.Encoding]::UTF8.GetString($m.ToArray())}
function Serialize($x){$x|ConvertTo-Json -Depth 50 -Compress}
$matrix='RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json';$a=(Blob $sha $matrix|ConvertFrom-Json).functions;$b=(Blob $parent $matrix|ConvertFrom-Json).functions
$refresh=(Get-Content (Join-Path $Root 'binding-summary.json') -Raw|ConvertFrom-Json).changedIds;$preserved=@()
foreach($id in $refresh){$now=@($a|Where-Object functionId -eq $id)[0];$old=@($b|Where-Object functionId -eq $id)[0];foreach($e in $old.auditEvidence){$ok=@($now.auditEvidence|Where-Object{(Serialize $_) -ceq (Serialize $e)}).Count -eq 1;$preserved+=[pscustomobject]@{id=$id;path=$e.path;priorAuditEvidenceRetained=$ok};if(-not $ok){throw 'Prior audit evidence not retained'}}}
$lineChecks=@();foreach($pair in @(@('state-before.ps1','validate-state.ps1'),@('pipeline-before.ps1','test-pipeline-pipeline.ps1'))){$stored=Blob $sha ('RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch25/'+$pair[0]);$prior=Blob $parent ('RobWork/scripts/industrialrobot/'+$pair[1]);$lineChecks+=[pscustomobject]@{stored=$pair[0];rawEqual=($stored -ceq $prior);equalAfterOnlyCRLFtoLF=($stored.Replace("`r`n","`n") -ceq $prior.Replace("`r`n","`n"));storedCRLF=([regex]::Matches($stored,"`r`n").Count);parentCRLF=([regex]::Matches($prior,"`r`n").Count);storedChars=$stored.Length;parentChars=$prior.Length}}
$semantics=@(
 @('Convert-ToIso','统一PS7 DateTime与PS5.1字符串，保留已有秒精度格式规则'),
 @('Compare-Iso','DateTimeOffset标准API比较绝对时刻；不再按文本序比较偏移时间'),
 @('Test-NonNegativeInteger','仅.NET Int32/Int64且非负，拒bool/浮点/数字字符串'),
 @('root','沿原RepoRoot/StateFile解析并真读JSON，无并行状态源'),
 @('schema','原schemaVersion与phase枚举'),
 @('heartbeat','原ISO外形/未来偏差检查；两项计数收紧'),
 @('fencing-presence','字段存在性+TryParseExact(D)；不证明持锁授权'),
 @('attempts','三类attempts统一非负整数谓词'),
 @('policy','原bool/autoMerge词表规则；计数和leaseMinutes同一整数谓词'),
 @('contract-index','沿原契约目录建立taskMap，非第二契约实现'),
 @('queue','原validate-task调用及队首claimability；非复用阶段准入'),
 @('phase','原currentTask/branch/base/tickToken/队首及headSha阶段约束'),
 @('acceptance','原acceptedHead与证据指针形态；非证据内容有效性审查'),
 @('failure-record','原返工记录/最近attempt/branch一致性保留'),
 @('run','原kind/task/存在性校验；租约和心跳按绝对时刻比较'),
 @('history','原history必需字段/已知任务引用；未主张全内容验证'),
 @('docRefs','原必需文档键与可选B1清单原入口'),
 @('narrative-limit','原pausedReason不超过200字，不替代协议变更'),
 @('verdict','原错误汇总/队首可领取性结果/退出语义')
)|ForEach-Object{[pscustomobject]@{functionId=('foundation-governance.validate-state.'+$_[0]);review=$_[1];decision='pending';status='pending'}}
$result=[pscustomobject]@{frozen=$sha;oldEvidenceChecks=$preserved;beforeLineEndingChecks=$lineChecks;semanticRows=$semantics;upstreamBoundary='TimerUtil.hpp 37-63三处明确不适合calendar time；沿用.NET标准类型/API、原Git/PS/validate-task，无上游扩展或第二运动学/状态机';changedCodeScope=@('RobWork/scripts/industrialrobot/validate-state.ps1','RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1');untouchedCode='产品、框架、vcpkg、真实state/queue/lock无冻结提交差异';notAuthorized=@('全schema分支','真实持锁授权','外部证据有效性','全T01/T02关闭','REUSE-T02','算法迁移','合入','远程同步')}
$result|ConvertTo-Json -Depth 12|Set-Content (Join-Path $Root 'semantic-review.json') -Encoding utf8
$lineChecks|ConvertTo-Json -Depth 5;Write-Output ('prior evidence preserved='+$preserved.Count);Write-Output ('semantic rows='+$semantics.Count)
