param([string]$Script,[string]$OutDir)
$ErrorActionPreference='Stop'
[IO.Directory]::CreateDirectory($OutDir)|Out-Null
$tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Script,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'parser errors'}
$f=$ast.Find({param($n)$n-is[Management.Automation.Language.FunctionDefinitionAst]-and$n.Name-eq'Get-FailedTestNames'},$true)
. ([ScriptBlock]::Create($f.Extent.Text))
$cases=@(
 @{name='disabled';xml='<testsuites><testsuite name="S"><testcase name="DISABLED_X" status="notrun" result="suppressed"/></testsuite></testsuites>';expected=@('<xml-no-executed-tests>')},
 @{name='mixed-skip-healthy';xml='<testsuites><testsuite name="S"><testcase name="Skip"><skipped/></testcase><testcase name="Good" status="run" result="completed"/></testsuite></testsuites>';expected=@()},
 @{name='mixed-disabled-error';xml='<testsuites><testsuite name="S"><testcase name="Disabled" status="notrun"/><testcase name="Error"><error/></testcase></testsuite></testsuites>';expected=@('S.Error')},
 @{name='multiple-failure-error';xml='<testsuites><testsuite name="A"><testcase name="F"><failure/></testcase></testsuite><testsuite name="B"><testcase name="E"><error/></testcase></testsuite></testsuites>';expected=@('A.F','B.E')},
 @{name='all-skipped-two';xml='<testsuites><testsuite name="A"><testcase name="X"><skipped/></testcase><testcase name="Y" status="notrun"/></testsuite></testsuites>';expected=@('<xml-no-executed-tests>')}
)
$results=@();foreach($c in $cases){$path=Join-Path $OutDir ($c.name+'.xml');[IO.File]::WriteAllText($path,$c.xml);$actual=@(Get-FailedTestNames $path);$results+=[pscustomobject]@{name=$c.name;expected=@($c.expected);actual=@($actual);passed=(($actual-join'|')-ceq($c.expected-join'|')-and$actual.Count-eq$c.expected.Count)}}
$summary=[pscustomobject]@{executed=$cases.Count;passed=@($results|Where-Object passed).Count;failed=@($results|Where-Object{-not$_.passed}).Count;skipped=0;cases=$results}
$summary|ConvertTo-Json -Depth 7|Set-Content (Join-Path $OutDir 'report.json') -Encoding UTF8
$summary|Format-List executed,passed,failed,skipped
if($summary.failed){exit 1}
