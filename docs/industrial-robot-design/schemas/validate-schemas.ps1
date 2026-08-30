# validate-schemas.ps1
# D3 Schema 层校验器：JSON Schema 关键字子集递归校验 + 文件卫生检查 + result-envelope 合法组合镜像检查。
# 兼容 Windows PowerShell 5.1（未使用三元运算符、??、链式运算符）。
# 用法：在任意目录运行 powershell -NoProfile -ExecutionPolicy Bypass -File .\validate-schemas.ps1
#       （脚本用 $PSScriptRoot 定位自身，通常从 schemas/ 父目录 docs/industrial-robot-design/ 调用）

$ErrorActionPreference = 'Stop'

$SchemaRoot = $PSScriptRoot

# ---------------------------------------------------------------- 关键字子集纪律

$AllowedKeywords = @(
    '$schema', '$id', 'title', 'description', 'type', 'properties', 'required',
    'additionalProperties', 'enum', 'items', 'minimum', 'maximum',
    'minLength', 'maxLength', 'pattern', 'minItems'
)

$AllowedTypeNames = @('object', 'array', 'string', 'number', 'integer', 'boolean', 'null')

$Script:Failures = New-Object System.Collections.Generic.List[string]
$Script:SchemaCount = 0
$Script:ValidExampleCount = 0
$Script:InvalidRejectedCount = 0
$Script:Schemas = @{}          # stem -> parsed schema

function Add-Failure {
    param([string]$Message)
    $Script:Failures.Add($Message) | Out-Null
}

# ---------------------------------------------------------------- 基础工具（大小写敏感）

function Get-PropertyNames {
    param($Object)
    $names = @()
    if ($null -ne $Object) {
        foreach ($p in $Object.PSObject.Properties) {
            $names += $p.Name
        }
    }
    return ,$names
}

function Get-PropertyValue {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    foreach ($p in $Object.PSObject.Properties) {
        if ($p.Name -ceq $Name) { return $p.Value }
    }
    return $null
}

function Test-ContainsOrdinal {
    param($Array, [string]$Item)
    if ($null -eq $Array) { return $false }
    foreach ($e in @($Array)) {
        if ([string]$e -ceq $Item) { return $true }
    }
    return $false
}

function Test-IsNumeric {
    param($Value)
    if ($null -eq $Value) { return $false }
    if ($Value -is [bool]) { return $false }
    if ($Value -is [double]) { return $true }
    if ($Value -is [int]) { return $true }
    if ($Value -is [long]) { return $true }
    if ($Value -is [decimal]) { return $true }
    if ($Value -is [Int16]) { return $true }
    if ($Value -is [UInt32]) { return $true }
    if ($Value -is [UInt64]) { return $true }
    return $false
}

# ---------------------------------------------------------------- 文件卫生检查（UTF-8 无 BOM / LF / 无非有限数）

function Read-JsonFileChecked {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -ge 3) {
        if ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            Add-Failure ("{0} : UTF-8 BOM present (must be BOM-less UTF-8)" -f $Path)
        }
    }
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($text.IndexOf("`r") -ge 0) {
        Add-Failure ("{0} : CR found (LF line endings required)" -f $Path)
    }
    # 大小写敏感匹配（-match 默认不区分大小写，会把 "proveNANce" 误判为 NaN）
    if ([regex]::IsMatch($text, '\bNaN\b|\bInfinity\b')) {
        Add-Failure ("{0} : non-finite number literal found (NaN/Infinity are forbidden)" -f $Path)
    }
    try {
        # PowerShell 7.5+ 会默认把 ISO 8601 字符串转换为 DateTime；JSON Schema 的
        # string 类型必须保留词法类型。DateKind 在 Windows PowerShell 5.1 不存在，
        # 因此仅在当前运行时提供该参数时显式选择 String。
        $convertFromJsonParameters = @{ InputObject = $text }
        if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) {
            $convertFromJsonParameters['DateKind'] = 'String'
        }
        $parsed = ConvertFrom-Json @convertFromJsonParameters
        return $parsed
    }
    catch {
        Add-Failure ("{0} : JSON parse error: {1}" -f $Path, $_.Exception.Message)
        return $null
    }
}

# ---------------------------------------------------------------- Schema 文件自身的关键字纪律检查

function Test-SchemaKeywords {
    param($SchemaNode, [string]$NodePath)
    if ($null -eq $SchemaNode) { return }
    if ($SchemaNode -isnot [System.Management.Automation.PSCustomObject]) {
        Add-Failure ("schema {0} : node is not an object" -f $NodePath)
        return
    }
    foreach ($p in $SchemaNode.PSObject.Properties) {
        if ($AllowedKeywords -notcontains $p.Name) {
            Add-Failure ("schema {0} : unsupported keyword '{1}' (subset discipline: see schemas/README.md section 2)" -f $NodePath, $p.Name)
        }
    }
    $typeValue = Get-PropertyValue $SchemaNode 'type'
    if ($null -ne $typeValue) {
        if ($AllowedTypeNames -notcontains [string]$typeValue) {
            Add-Failure ("schema {0} : unsupported type value '{1}' or array-form type (single type string required)" -f $NodePath, $typeValue)
        }
    }
    $propertiesNode = Get-PropertyValue $SchemaNode 'properties'
    if ($null -ne $propertiesNode) {
        foreach ($sp in $propertiesNode.PSObject.Properties) {
            Test-SchemaKeywords -SchemaNode $sp.Value -NodePath ("{0}.{1}" -f $NodePath, $sp.Name)
        }
    }
    $itemsNode = Get-PropertyValue $SchemaNode 'items'
    if ($null -ne $itemsNode) {
        Test-SchemaKeywords -SchemaNode $itemsNode -NodePath ("{0}.items" -f $NodePath)
    }
}

# ---------------------------------------------------------------- 类型与枚举匹配

function Test-TypeMatch {
    param($Value, [string]$TypeName)
    switch ($TypeName) {
        'object'  { if ($Value -is [System.Management.Automation.PSCustomObject]) { return $true } else { return $false } }
        'array'   { if ($Value -is [System.Array]) { return $true } else { return $false } }
        'string'  { if ($Value -is [string]) { return $true } else { return $false } }
        'boolean' { if ($Value -is [bool]) { return $true } else { return $false } }
        'number'  { if (Test-IsNumeric $Value) { return $true } else { return $false } }
        'integer' {
            if ($Value -is [bool]) { return $false }
            if ($Value -is [int] -or $Value -is [long] -or $Value -is [Int16] -or $Value -is [UInt32] -or $Value -is [UInt64]) { return $true }
            if ($Value -is [double]) {
                $diff = [Math]::Abs($Value - [Math]::Floor($Value))
                if ($diff -lt 1e-9) { return $true } else { return $false }
            }
            return $false
        }
        'null'    { if ($null -eq $Value) { return $true } else { return $false } }
    }
    return $false
}

function Test-EnumMatch {
    param($Value, $EnumValues)
    foreach ($e in @($EnumValues)) {
        if ($null -eq $Value -and $null -eq $e) { return $true }
        if ($null -eq $Value) { continue }
        if ($Value -is [string] -and $e -is [string]) {
            if ([string]::Equals($Value, $e, [System.StringComparison]::Ordinal)) { return $true }
        }
        else {
            if ($Value -ceq $e) { return $true }
        }
    }
    return $false
}

# ---------------------------------------------------------------- 核心递归校验器（子集语义）

function Test-AgainstSchema {
    param($Schema, $Value, [string]$Path)

    if ($null -eq $Schema) { return }

    # PS 5.1 ConvertFrom-Json 数组怪癖归一化：
    #   单元素数组会被解包为标量，空数组会变成 $null。
    # 当 schema 声明数组语义（type=array 或带 items/minItems）时，把值还原为数组。
    $isArrayContext = $false
    $typeNodeEarly = Get-PropertyValue $Schema 'type'
    if ([string]$typeNodeEarly -ceq 'array') { $isArrayContext = $true }
    if ($null -ne (Get-PropertyValue $Schema 'items')) { $isArrayContext = $true }
    if ($null -ne (Get-PropertyValue $Schema 'minItems')) { $isArrayContext = $true }
    if ($isArrayContext) {
        if ($null -eq $Value) { $Value = @() }
        elseif ($Value -isnot [System.Array]) { $Value = @($Value) }
    }

    # enum：对任意类型生效，大小写敏感
    $enumNode = Get-PropertyValue $Schema 'enum'
    if ($null -ne $enumNode) {
        if (-not (Test-EnumMatch -Value $Value -EnumValues @($enumNode))) {
            $shown = ''
            if ($null -ne $Value) { $shown = [string]$Value }
            Add-Failure ("{0} : value '{1}' not in enum [{2}]" -f $Path, $shown, (@($enumNode) -join ', '))
        }
    }

    # type：单值类型
    $typeNode = Get-PropertyValue $Schema 'type'
    if ($null -ne $typeNode) {
        if (-not (Test-TypeMatch -Value $Value -TypeName ([string]$typeNode))) {
            $got = 'null'
            if ($null -ne $Value) { $got = $Value.GetType().Name }
            Add-Failure ("{0} : expected type '{1}', got '{2}'" -f $Path, $typeNode, $got)
        }
    }

    # string 关键字
    if ($Value -is [string]) {
        $minLength = Get-PropertyValue $Schema 'minLength'
        if ($null -ne $minLength) {
            if ($Value.Length -lt [int]$minLength) {
                Add-Failure ("{0} : string length {1} is below minLength {2}" -f $Path, $Value.Length, $minLength)
            }
        }
        $maxLength = Get-PropertyValue $Schema 'maxLength'
        if ($null -ne $maxLength) {
            if ($Value.Length -gt [int]$maxLength) {
                Add-Failure ("{0} : string length {1} is above maxLength {2}" -f $Path, $Value.Length, $maxLength)
            }
        }
        $pattern = Get-PropertyValue $Schema 'pattern'
        if ($null -ne $pattern) {
            if (-not [regex]::IsMatch($Value, [string]$pattern)) {
                Add-Failure ("{0} : value '{1}' does not match pattern '{2}'" -f $Path, $Value, $pattern)
            }
        }
    }

    # number 关键字（含边界）
    if (Test-IsNumeric $Value) {
        $minimum = Get-PropertyValue $Schema 'minimum'
        if ($null -ne $minimum) {
            if ([double]$Value -lt [double]$minimum) {
                Add-Failure ("{0} : value {1} is below minimum {2}" -f $Path, $Value, $minimum)
            }
        }
        $maximum = Get-PropertyValue $Schema 'maximum'
        if ($null -ne $maximum) {
            if ([double]$Value -gt [double]$maximum) {
                Add-Failure ("{0} : value {1} is above maximum {2}" -f $Path, $Value, $maximum)
            }
        }
    }

    # array 关键字
    if ($Value -is [System.Array]) {
        $arr = @($Value)
        $minItems = Get-PropertyValue $Schema 'minItems'
        if ($null -ne $minItems) {
            if ($arr.Count -lt [int]$minItems) {
                Add-Failure ("{0} : item count {1} is below minItems {2}" -f $Path, $arr.Count, $minItems)
            }
        }
        $itemsNode = Get-PropertyValue $Schema 'items'
        if ($null -ne $itemsNode) {
            for ($i = 0; $i -lt $arr.Count; $i++) {
                Test-AgainstSchema -Schema $itemsNode -Value $arr[$i] -Path ("{0}[{1}]" -f $Path, $i)
            }
        }
    }

    # object 关键字
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        $presentNames = Get-PropertyNames $Value

        $requiredNode = Get-PropertyValue $Schema 'required'
        if ($null -ne $requiredNode) {
            foreach ($r in @($requiredNode)) {
                if (-not (Test-ContainsOrdinal -Array $presentNames -Item ([string]$r))) {
                    Add-Failure ("{0} : missing required property '{1}'" -f $Path, $r)
                }
            }
        }

        $additional = Get-PropertyValue $Schema 'additionalProperties'
        $propertiesNode = Get-PropertyValue $Schema 'properties'
        # 注意：直接赋值（不要 @(...) 包裹命令调用，@() 只收集不展开会得到嵌套数组）
        $knownNames = Get-PropertyNames $propertiesNode
        if ($additional -eq $false) {
            foreach ($name in $presentNames) {
                if (-not (Test-ContainsOrdinal -Array $knownNames -Item $name)) {
                    Add-Failure ("{0} : unknown property '{1}' (additionalProperties is false)" -f $Path, $name)
                }
            }
        }

        if ($null -ne $propertiesNode) {
            foreach ($sp in $propertiesNode.PSObject.Properties) {
                if (Test-ContainsOrdinal -Array $presentNames -Item $sp.Name) {
                    $childValue = Get-PropertyValue -Object $Value -Name $sp.Name
                    Test-AgainstSchema -Schema $sp.Value -Value $childValue -Path ("{0}.{1}" -f $Path, $sp.Name)
                }
            }
        }
    }
}

# ---------------------------------------------------------------- result-envelope 合法组合镜像检查
# 子集无法表达互斥组合，此处按 architecture/evaluation-semantics.md §2 冻结表镜像实现（演示性合同测试）。

function Test-ResultEnvelopeCombination {
    param($Instance, [string]$Path)
    $outcome = Get-PropertyValue -Object $Instance -Name 'outcome'
    $status = Get-PropertyValue -Object $Instance -Name 'engineeringStatus'
    $completeness = Get-PropertyValue -Object $Instance -Name 'payloadCompleteness'
    if ($null -eq $outcome -or $null -eq $status -or $null -eq $completeness) { return }

    $legal = $false
    if ([string]$outcome -ceq 'Completed') {
        if (@('Pass', 'Warning', 'Infeasible', 'DataInsufficient') -ccontains [string]$status) {
            if ([string]$completeness -ceq 'Complete') { $legal = $true }
        }
    }
    else {
        if ([string]$status -ceq 'NotEvaluated') {
            if (@('Partial', 'None') -ccontains [string]$completeness) { $legal = $true }
        }
    }
    if (-not $legal) {
        Add-Failure ("{0} : illegal orthogonal status combination outcome={1} + engineeringStatus={2} + payloadCompleteness={3} (legal combinations: architecture/evaluation-semantics.md section 2)" -f $Path, $outcome, $status, $completeness)
    }
}

# ---------------------------------------------------------------- 装载 Schema

$schemaFiles = @(Get-ChildItem -Path $SchemaRoot -Recurse -Filter '*.schema.json' | Sort-Object -Property FullName)
$SchemaCount = $schemaFiles.Count

$exampleCoverage = @{}
foreach ($sf in $schemaFiles) {
    $parsed = Read-JsonFileChecked -Path $sf.FullName
    if ($null -eq $parsed) { continue }
    Test-SchemaKeywords -SchemaNode $parsed -NodePath $sf.Name
    if ($null -eq (Get-PropertyValue $parsed '$schema')) { Add-Failure ("{0} : missing '$schema' declaration" -f $sf.Name) }
    if ($null -eq (Get-PropertyValue $parsed '$id')) { Add-Failure ("{0} : missing '$id'" -f $sf.Name) }
    if ($null -eq (Get-PropertyValue $parsed 'title')) { Add-Failure ("{0} : missing 'title'" -f $sf.Name) }
    $rootType = Get-PropertyValue $parsed 'type'
    if ([string]$rootType -cne 'object') { Add-Failure ("{0} : root type must be 'object'" -f $sf.Name) }

    $stem = $sf.Name -replace '\.schema\.json$', ''
    if ($Script:Schemas.ContainsKey($stem)) {
        Add-Failure ("duplicate schema stem '{0}'" -f $stem)
    }
    else {
        $Script:Schemas.Add($stem, $parsed)
        $exampleCoverage[$stem] = $false
    }
}

# ---------------------------------------------------------------- 校验一个实例（返回新增失败数）

function Test-ExampleAgainstSchema {
    param([string]$ExamplePath, [string]$SchemaStem)
    $before = $Script:Failures.Count
    $instance = Read-JsonFileChecked -Path $ExamplePath
    if ($null -ne $instance) {
        $schema = $Script:Schemas[$SchemaStem]
        if ($null -eq $schema) {
            Add-Failure ("{0} : no schema named '{1}.schema.json' found" -f $ExamplePath, $SchemaStem)
        }
        else {
            Test-AgainstSchema -Schema $schema -Value $instance -Path (Split-Path -Leaf $ExamplePath)
            if ($SchemaStem -ceq 'result-envelope') {
                Test-ResultEnvelopeCombination -Instance $instance -Path (Split-Path -Leaf $ExamplePath)
            }
        }
    }
    return ($Script:Failures.Count - $before)
}

# ---------------------------------------------------------------- 合法示例（必须全部通过）

$validExampleFiles = @()
if (Test-Path (Join-Path $SchemaRoot 'examples')) {
    $validExampleFiles = @(Get-ChildItem -Path (Join-Path $SchemaRoot 'examples') -Recurse -Filter '*.example.json' |
        Where-Object { $_.FullName -notmatch '\\invalid\\' } |
        Sort-Object -Property FullName)
}

foreach ($ef in $validExampleFiles) {
    $stem = $ef.Name -replace '\.example\.json$', ''
    $newErrors = Test-ExampleAgainstSchema -ExamplePath $ef.FullName -SchemaStem $stem
    if ($newErrors -gt 0) {
        Write-Output ("[FAIL] valid example rejected: {0} ({1} error(s))" -f $ef.FullName, $newErrors)
    }
    else {
        $ValidExampleCount = $ValidExampleCount + 1
        if ($exampleCoverage.ContainsKey($stem)) { $exampleCoverage[$stem] = $true }
        else { Add-Failure ("example '{0}' has no matching schema" -f $ef.Name) }
    }
}

foreach ($key in @($exampleCoverage.Keys)) {
    if (-not $exampleCoverage[$key]) {
        Add-Failure ("schema '{0}' has no valid example (examples/{0}.example.json required)" -f $key)
    }
}

# ---------------------------------------------------------------- 非法示例（必须被拒绝）

$invalidDir = Join-Path $SchemaRoot 'examples\invalid'
$invalidExampleFiles = @()
if (Test-Path $invalidDir) {
    $invalidExampleFiles = @(Get-ChildItem -Path $invalidDir -Filter '*.example.json' | Sort-Object -Property FullName)
}

foreach ($ef in $invalidExampleFiles) {
    $nameStem = $ef.Name -replace '\.example\.json$', ''
    $schemaStem = $nameStem -split '\.' | Select-Object -First 1
    $before = $Script:Failures.Count
    $newErrors = Test-ExampleAgainstSchema -ExamplePath $ef.FullName -SchemaStem $schemaStem
    if ($newErrors -gt 0) {
        $firstError = $Script:Failures[$before]
        Write-Output ("[OK]   invalid example correctly rejected: {0}" -f $ef.Name)
        Write-Output ("       first reason: {0}" -f $firstError)
        $InvalidRejectedCount = $InvalidRejectedCount + 1
        # 从全局失败列表移除预期失败，保持最终统计干净
        for ($i = $Script:Failures.Count - 1; $i -ge $before; $i--) {
            $Script:Failures.RemoveAt($i) | Out-Null
        }
    }
    else {
        Add-Failure ("invalid example was NOT rejected: {0} (expected validation failure)" -f $ef.FullName)
    }
}

if ($invalidExampleFiles.Count -lt 3) {
    Add-Failure ("expected at least 3 intentionally invalid examples under examples/invalid/, found {0}" -f $invalidExampleFiles.Count)
}

# D12：每个 Schema 的负例最低覆盖（>= 3 个），负例必须覆盖五类中的可表达类别
$invalidPerSchema = @{}
foreach ($ef in $invalidExampleFiles) {
    $stem = ($ef.Name -replace '\.example\.json$', '') -split '\.' | Select-Object -First 1
    if ($invalidPerSchema.ContainsKey($stem)) { $invalidPerSchema[$stem] = $invalidPerSchema[$stem] + 1 }
    else { $invalidPerSchema[$stem] = 1 }
}
foreach ($key in @($exampleCoverage.Keys)) {
    $count = 0
    if ($invalidPerSchema.ContainsKey($key)) { $count = $invalidPerSchema[$key] }
    if ($count -lt 3) {
        Add-Failure ("schema '{0}' has {1} invalid example(s); at least 3 required (missing-required / bad-enum-or-value / cross-field-illegal / future-version / dangling-ref-or-duplicate)" -f $key, $count)
    }
}

# ---------------------------------------------------------------- 汇总

Write-Output ''
Write-Output ("{0} schemas, {1} examples valid, {2} invalid correctly rejected" -f $SchemaCount, $ValidExampleCount, $InvalidRejectedCount)

if ($Script:Failures.Count -gt 0) {
    Write-Output ''
    Write-Output ("FAILED with {0} unexpected error(s):" -f $Script:Failures.Count)
    foreach ($f in $Script:Failures) {
        Write-Output ("  - {0}" -f $f)
    }
    exit 1
}

Write-Output 'All checks passed.'
exit 0
