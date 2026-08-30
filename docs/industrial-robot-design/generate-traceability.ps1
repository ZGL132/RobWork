param(
    [string]$RequirementsPath = (Join-Path $PSScriptRoot 'requirements.md'),
    [string]$OutputPath = (Join-Path $PSScriptRoot 'requirement-traceability.csv')
)

$ErrorActionPreference = 'Stop'

function Expand-RequirementCell {
    param([string]$Cell)

    $ids = [System.Collections.Generic.List[string]]::new()
    $currentPrefix = $null
    $pattern = '(?:(?<prefix>[A-Z][A-Z0-9-]*-))?(?<start>\d+)(?:～(?<end>\d+))?'

    foreach ($match in [regex]::Matches($Cell, $pattern)) {
        if ($match.Groups['prefix'].Success) {
            $currentPrefix = $match.Groups['prefix'].Value
        }
        if ([string]::IsNullOrWhiteSpace($currentPrefix)) {
            # WP-00 计划 §5.1：前缀续接仅在已有前缀时成立；无前缀可续接的裸编号属非法输入。
            throw ("Invalid requirement cell '{0}': number token {1} has no preceding requirement prefix." -f $Cell, $match.Groups['start'].Value)
        }

        $start = [int]$match.Groups['start'].Value
        $end = $start
        if ($match.Groups['end'].Success) {
            $end = [int]$match.Groups['end'].Value
            if ($end -lt $start) {
                # WP-00 计划 §8.2：反向/非法范围必须失败，不得静默跳过。
                throw ("Invalid requirement range '{0}{1:D2}～{2:D2}' in cell '{3}': end is less than start." -f $currentPrefix, $start, $end, $Cell)
            }
        }

        for ($number = $start; $number -le $end; $number++) {
            $id = '{0}{1:D2}' -f $currentPrefix, $number
            if (-not $ids.Contains($id)) {
                $ids.Add($id)
            }
        }
    }

    return $ids
}

function Get-Release {
    param([string]$RequirementId)

    switch -Regex ($RequirementId) {
        '^NFR-PERF-0[456]$' { return 'R2' }
        '^OPT-(01|02|03|04|06|07|08)$' { return 'R1/R2' }
        '^OPT-(05|09|10)$' { return 'R2' }
        '^CON-04$' { return 'R1/R2' }
        default { return 'R1' }
    }
}

# 工作包映射访问路径（WP-00 计划 §5.2）：D8 显式映射即特殊规则，优先于任何前缀推断；
# 需求文档中出现而映射表未登记的 ID 一律失败，不得按前缀猜测主包。
# 前置条件：调用前 $requirementMap 已构建，且映射表与需求文档的双向校验已通过。
function Get-WorkPackages {
    param([string]$RequirementId)

    if ($requirementMap.ContainsKey($RequirementId)) {
        return $requirementMap[$RequirementId]
    }

    $prefix = if ($RequirementId -match '^([A-Z][A-Z0-9-]*-)\d+$') { $Matches[1] } else { $RequirementId }
    throw "No work-package mapping for requirement $RequirementId (prefix $prefix). Register it in the D8 mapping table."
}

# CSV 写入路径（WP-00 计划 §5.2.4）：先写同目录临时文件，再原子替换正式文件；
# 任一步失败仅清理临时文件，正式 CSV 保持原字节不变。
function Write-Utf8BomCrlfAtomically {
    param([string]$Path, [string]$Text)

    $directory = Split-Path -Parent $Path
    if ([string]::IsNullOrWhiteSpace($directory)) {
        $directory = (Get-Location).Path
    }
    $tempFile = Join-Path $directory ("ird-csv-gen-" + [guid]::NewGuid() + '.tmp')
    try {
        $utf8Bom = New-Object System.Text.UTF8Encoding($true)
        [System.IO.File]::WriteAllText($tempFile, $Text, $utf8Bom)
        if (Test-Path -LiteralPath $Path) {
            # PowerShell 会把 $null 编组为空字符串绑定到 string 参数，导致 Replace 抛出
            # "路径的形式不合法"；必须用 NullString::Value 传递真正的 null 备份文件名。
            [System.IO.File]::Replace($tempFile, $Path, [System.Management.Automation.Language.NullString]::Value)
        }
        else {
            [System.IO.File]::Move($tempFile, $Path)
        }
        $tempFile = $null
    }
    finally {
        if ($tempFile -and (Test-Path -LiteralPath $tempFile)) {
            Remove-Item -LiteralPath $tempFile -Force
        }
    }
}

# D8 显式需求映射表（来源：agent-tasks 全部任务卡第 1 字段"Task ID / 需求 ID / ADR / 阶段"
# 的需求覆盖反向索引；primary 取 D6 计划"主要需求"列（原 CSV work_package 首项），
# tasks/tests 取卡内声明的覆盖与代表性测试名，evidence 为规范化证据目录，
# gate 按 §16 阶段（含 A-GATE 场景明示编号）推导。任务 ID 必须真实存在于 agent-tasks/。
$requirementMap = @{
    'ARC-01' = @{ primary = 'WP-04'; supporting = @('WP-03'); tasks = @('WP-04-T01','WP-04-T02','WP-04-T05','WP-03-T01','WP-03-T02','WP-03-T03','WP-03-T04','WP-03-T05','WP-14-T06'); tests = @('PathSafetyTest','CommandRevisionTest','ProjectCommandContractTest'); evidence = 'out/test-evidence/wp-04/<run-id>/'; gate = 'A-GATE-02' }
    'ARC-02' = @{ primary = 'WP-01'; supporting = @(); tasks = @('WP-01-T01','WP-01-T02'); tests = @('old-plugin-dependency','widget-header','unregistered-library'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'ARC-03' = @{ primary = 'WP-06'; supporting = @('WP-03'); tasks = @('WP-06-T01','WP-06-T02','WP-06-T03','WP-06-T05','WP-03-T01','WP-03-T02','WP-03-T03','WP-03-T04','WP-03-T05'); tests = @('CanonicalModelTest','NameMapTest','DualCompileTest'); evidence = 'out/test-evidence/wp-06/<run-id>/'; gate = 'A-GATE-01～07/B-GATE' }
    'ARC-04' = @{ primary = 'WP-06'; supporting = @('WP-03'); tasks = @('WP-06-T01','WP-06-T02','WP-06-T03','WP-06-T05','WP-03-T02','WP-03-T03','WP-03-T04','WP-03-T05','WP-13-T07'); tests = @('CanonicalModelTest','NameMapTest','DualCompileTest'); evidence = 'out/test-evidence/wp-06/<run-id>/'; gate = 'A-GATE-06/B-GATE' }
    'ARC-05' = @{ primary = 'WP-07'; supporting = @('WP-03'); tasks = @('WP-07-T01','WP-07-T02','WP-07-T03','WP-07-T04','WP-07-T05','WP-03-T02','WP-03-T03','WP-03-T04','WP-03-T05','WP-15-T08','WP-20-T08'); tests = @('PolicyNormalizationTest','unknownDistanceFallback','PathProtocolTest'); evidence = 'out/test-evidence/wp-07/<run-id>/'; gate = 'A-GATE-07/C-GATE' }
    'CON-01' = @{ primary = 'WP-05'; supporting = @('WP-04'); tasks = @('WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04','WP-04-T01','WP-04-T02','WP-04-T04','WP-04-T05'); tests = @('sliceHash','snapshotId','ResultStatusTest'); evidence = 'out/test-evidence/wp-05/<run-id>/'; gate = 'A-GATE-01～07' }
    'CON-02' = @{ primary = 'WP-05'; supporting = @(); tasks = @('WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04','WP-20-T06'); tests = @('sliceHash','snapshotId','ResultStatusTest'); evidence = 'out/test-evidence/wp-05/<run-id>/'; gate = 'A-GATE-02' }
    'CON-03' = @{ primary = 'WP-04'; supporting = @('WP-05'); tasks = @('WP-04-T01','WP-04-T02','WP-04-T03','WP-04-T04','WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04'); tests = @('PathSafetyTest','CommandRevisionTest','ProjectCommandContractTest'); evidence = 'out/test-evidence/wp-04/<run-id>/'; gate = 'A-GATE-01～07/B-GATE' }
    'CON-04' = @{ primary = 'WP-08'; supporting = @('WP-05'); tasks = @('WP-08-T02','WP-08-T04','WP-08-T05','WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04','WP-20-T05','WP-21-T03','WP-23-T02'); tests = @('RequestIdentityTest','CacheCheckpointTest','BoundedParallelismTest'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-01～07/D-GATE' }
    'CON-05' = @{ primary = 'WP-05'; supporting = @(); tasks = @('WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04','WP-14-T06'); tests = @('sliceHash','snapshotId','ResultStatusTest'); evidence = 'out/test-evidence/wp-05/<run-id>/'; gate = 'A-GATE-01～07' }
    'CON-06' = @{ primary = 'WP-05'; supporting = @('WP-06','WP-07'); tasks = @('WP-05-T01','WP-05-T02','WP-05-T03','WP-05-T04','WP-06-T01','WP-06-T02','WP-06-T03','WP-06-T05','WP-07-T01','WP-07-T02','WP-07-T04','WP-07-T05'); tests = @('sliceHash','snapshotId','ResultStatusTest'); evidence = 'out/test-evidence/wp-05/<run-id>/'; gate = 'A-GATE-01～07/C-GATE' }
    'DYN-01' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T01','WP-17-T02'); tests = @('SemanticFreezeTest','SemanticsVersionRegistered','ZeroSpeedCoulombContinuous'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-02' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T01','WP-17-T02'); tests = @('SemanticFreezeTest','SemanticsVersionRegistered','ZeroSpeedCoulombContinuous'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-03' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T03','WP-18-T04'); tests = @('PowerEnergyTest','WPlusTrapezoidMatchesGolden','PowerSignConvention'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-04' = @{ primary = 'WP-18'; supporting = @('WP-17'); tasks = @('WP-18-T01','WP-18-T02','WP-18-T03','WP-18-T04','WP-18-T05','WP-17-T06'); tests = @('MappingSemanticsTest','ConstantsAreFrozen','driveTrainInertiaFormulaVersion'); evidence = 'out/test-evidence/wp-18/<run-id>/'; gate = 'C-GATE' }
    'DYN-05' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T04'); tests = @('ForwardDynamicsTest','HAndHalfConvergesWithinLimits','NotConvergedReportsDiagnostic'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-06' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T05'); tests = @('InsufficientDataTest','MissingPropertiesDegradesToScreening','MissingFrictionContinuesZeroFriction'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-07' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T03'); tests = @('PowerEnergyTest','WPlusTrapezoidMatchesGolden','PowerSignConvention'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'DYN-08' = @{ primary = 'WP-17'; supporting = @(); tasks = @('WP-17-T03','WP-17-T07'); tests = @('PowerEnergyTest','DynamicsGuiTest','EnvelopeColumnsAndUnitsMatchSpecification'); evidence = 'out/test-evidence/wp-17/<run-id>/'; gate = 'C-GATE' }
    'ERR-01' = @{ primary = 'WP-09'; supporting = @(); tasks = @('WP-09-T01','WP-09-T02','WP-09-T03','WP-09-T05'); tests = @('DiagnosticSchemaTest','CatalogTermsTest','DiagnosticCatalogContractTest'); evidence = 'out/test-evidence/wp-09/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'EVI-01' = @{ primary = 'WP-05'; supporting = @('WP-12'); tasks = @('WP-05-T02','WP-05-T03','WP-05-T04','WP-05-T05','WP-12-T01','WP-12-T02','WP-12-T03','WP-12-T04','WP-12-T05','WP-12-T06'); tests = @('snapshotId','ResultStatusTest','EvaluatorContractTest'); evidence = 'out/test-evidence/wp-05/<run-id>/'; gate = 'A-GATE-01～07/B-GATE' }
    'KIN-01' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T01','WP-15-T03'); tests = @('FkMatchesAnalyticReferencePoses','FkRejectsUnresolvableReference','FkWorldJointAxesFollowCanonicalChain'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-02' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T02'); tests = @('IkCandidateOrderingTest.ApplicableBeforeResidual','IkMultiStartConvergesWithinTaskTolerance','IkFiltersResidualOverToleranceWithReason'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-03' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T06'); tests = @('BatchRequiresCompleteRunIdentity','BatchFixedSequenceReproducesByteStable','LateCallbackAppendsOriginalBranchHistoryOnly'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-04' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T04'); tests = @('CoverageDenominatorMatchesFrozenDefinition','GridSamplesIncludeBoundary','UserCancelYieldsCanceledNotEvaluatedPartial'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-05' = @{ primary = 'WP-15'; supporting = @('WP-07'); tasks = @('WP-15-T05','WP-07-T01','WP-07-T02','WP-07-T03'); tests = @('EvidenceMatchesSharedEvaluatorVerdict','MissingDetectorYieldsDataInsufficient','WordingFrozenToQualifiedNoCollision'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-06' = @{ primary = 'WP-15'; supporting = @('WP-10'); tasks = @('WP-15-T07','WP-10-T01','WP-10-T02'); tests = @('CandidatePreviewDoesNotCreateRevision','DoubleClickOnlyChangesSessionPose','ExplicitApplyGoesThroughCommandPort'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-07' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T07'); tests = @('CandidatePreviewDoesNotCreateRevision','DoubleClickOnlyChangesSessionPose','ExplicitApplyGoesThroughCommandPort'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'KIN-08' = @{ primary = 'WP-15'; supporting = @(); tasks = @('WP-15-T07'); tests = @('CandidatePreviewDoesNotCreateRevision','DoubleClickOnlyChangesSessionPose','ExplicitApplyGoesThroughCommandPort'); evidence = 'out/test-evidence/wp-15/<run-id>/'; gate = 'B-GATE' }
    'MDL-01' = @{ primary = 'WP-13'; supporting = @('WP-22'); tasks = @('WP-13-T01','WP-13-T02','WP-22-T06'); tests = @('ModelFixtureTest','ProjectEntryModelTest','BlankAndSampleCreateThroughProjectCommand'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE/R-GATE' }
    'MDL-02' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T01','WP-13-T04'); tests = @('ModelFixtureTest','NotRepresentable','DhConversionTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-03' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T01','WP-13-T03'); tests = @('ModelFixtureTest','Defaulted','UrdfImportTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-04' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T01'); tests = @('ModelFixtureTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-05' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T05'); tests = @('MaterialToolTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-06' = @{ primary = 'WP-13'; supporting = @('WP-06'); tasks = @('WP-13-T06','WP-06-T01','WP-06-T02','WP-06-T04'); tests = @('CompiledRobotArtifacts','RuntimeCompileTest','CanonicalModelTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-07' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T05','WP-13-T08'); tests = @('MaterialToolTest','ModelingGuiTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-08' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T02','WP-13-T07','WP-21-T05'); tests = @('robotId','buildCommand','DomainEditorTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-09' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T04','WP-06-T04'); tests = @('NotRepresentable','DhConversionTest','AxisAdapterTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-10' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T04','WP-06-T04'); tests = @('NotRepresentable','DhConversionTest','AxisAdapterTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-11' = @{ primary = 'WP-13'; supporting = @('WP-22'); tasks = @('WP-13-T01','WP-13-T03','WP-22-T06'); tests = @('ModelFixtureTest','UrdfImportTest','UrdfCreatesProjectInsteadOfImportingModel'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE/R-GATE' }
    'MDL-12' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T01','WP-13-T03'); tests = @('ModelFixtureTest','Defaulted','UrdfImportTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-13' = @{ primary = 'WP-13'; supporting = @(); tasks = @('WP-13-T02','WP-13-T05','WP-13-T08'); tests = @('robotId','buildCommand','DomainEditorTest'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'MDL-14' = @{ primary = 'WP-13'; supporting = @('WP-06'); tasks = @('WP-13-T06','WP-13-T07','WP-06-T01','WP-06-T02','WP-06-T04'); tests = @('CompiledRobotArtifacts','RuntimeCompileTest','objectId'); evidence = 'out/test-evidence/wp-13/<run-id>/'; gate = 'B-GATE' }
    'NFR-COR-01' = @{ primary = 'WP-02'; supporting = @('WP-23'); tasks = @('WP-02-T01','WP-02-T02','WP-02-T03','WP-02-T04','WP-23-T01','WP-23-T03','WP-23-T05'); tests = @('LoaderRejectsMissingSampleFile','LoaderRejectsSha256Mismatch','LoaderRejectsDuplicateSampleId'); evidence = 'out/test-evidence/wp-02/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-COR-02' = @{ primary = 'WP-23'; supporting = @('WP-08'); tasks = @('WP-23-T01','WP-23-T03','WP-23-T04','WP-23-T05','WP-05-T04','WP-05-T05','WP-08-T01','WP-08-T02','WP-08-T04','WP-08-T05'); tests = @('SystemSuiteTest','BenchmarkTest','DeterminismTest'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-COR-03' = @{ primary = 'WP-03'; supporting = @('WP-11','WP-23'); tasks = @('WP-03-T01','WP-03-T03','WP-03-T04','WP-11-T02','WP-11-T05','WP-23-T01','WP-23-T03','WP-23-T05'); tests = @('Angle','Length','SemanticsTest'); evidence = 'out/test-evidence/wp-03/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-COR-04' = @{ primary = 'WP-12'; supporting = @('WP-05','WP-23'); tasks = @('WP-12-T01','WP-12-T02','WP-12-T03','WP-12-T04','WP-12-T05','WP-12-T06','WP-05-T01','WP-05-T03','WP-05-T05','WP-23-T01','WP-23-T03','WP-23-T05'); tests = @('reportId','DesignVariantTest','PdfReportRenderer'); evidence = 'out/test-evidence/wp-12/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-COR-05' = @{ primary = 'WP-07'; supporting = @('WP-23'); tasks = @('WP-07-T01','WP-07-T02','WP-07-T03','WP-07-T04','WP-06-T03','WP-06-T04','WP-15-T08','WP-20-T08','WP-23-T01','WP-23-T03','WP-23-T05'); tests = @('PolicyNormalizationTest','unknownDistanceFallback','PathProtocolTest'); evidence = 'out/test-evidence/wp-07/<run-id>/'; gate = 'B-GATE/C-GATE' }
    'NFR-DEP-01' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T01','WP-24-T02','WP-01-T03','WP-01-T04'); tests = @('InstallerScriptTest','package.ps1','verify-package.ps1'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'R-GATE' }
    'NFR-DEP-02' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T01','WP-24-T02','WP-01-T04'); tests = @('InstallerScriptTest','package.ps1','verify-package.ps1'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'R-GATE' }
    'NFR-DEP-03' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T02','WP-24-T03'); tests = @('package.ps1','verify-package.ps1','WP-24-T03 检查表首项'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'R-GATE' }
    'NFR-DEP-04' = @{ primary = 'WP-04'; supporting = @('WP-24'); tasks = @('WP-04-T05','WP-24-T03'); tests = @('SchemaUpgradeTest','ProjectQueryContractTest','WP-24-T03 检查表首项'); evidence = 'out/test-evidence/wp-04/<run-id>/'; gate = 'R-GATE' }
    'NFR-DEP-05' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T03','WP-24-T04','WP-01-T05','WP-17-T04'); tests = @('WP-24-T03 检查表首项','InstallerScriptTest','t05-missing-fields'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'A-GATE-01～07/R-GATE' }
    'NFR-MNT-01' = @{ primary = 'WP-03'; supporting = @('WP-01'); tasks = @('WP-03-T02','WP-03-T05','WP-01-T02','WP-01-T03'); tests = @('DomainValuesTest','IdentityTest','check-boundaries.ps1'); evidence = 'out/test-evidence/wp-03/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-02' = @{ primary = 'WP-01'; supporting = @('WP-24'); tasks = @('WP-01-T01','WP-01-T02'); tests = @('old-plugin-dependency','widget-header','unregistered-library'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-03' = @{ primary = 'WP-03'; supporting = @('WP-09'); tasks = @('WP-03-T02','WP-03-T05','WP-09-T01','WP-09-T02','WP-09-T03','WP-09-T05'); tests = @('DomainValuesTest','IdentityTest','check-boundaries.ps1'); evidence = 'out/test-evidence/wp-03/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-04' = @{ primary = 'WP-01'; supporting = @(); tasks = @('WP-03-T01'); tests = @('Angle','Length'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-05' = @{ primary = 'WP-01'; supporting = @(); tasks = @('WP-01-T02'); tests = @('WP-01-T02 原生构建断言'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-06' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T01','WP-24-T03','WP-24-T05'); tests = @('InstallerScriptTest','WP-24-T03 检查表首项','WP-24-T05 检查表首项'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-MNT-07' = @{ primary = 'WP-01'; supporting = @('WP-06','WP-07'); tasks = @('WP-01-T01','WP-06-T01','WP-06-T02','WP-06-T05','WP-07-T04','WP-07-T05'); tests = @('old-plugin-dependency','widget-header','unregistered-library'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'A-GATE-01～07/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-PERF-01' = @{ primary = 'WP-23'; supporting = @('WP-10'); tasks = @('WP-23-T03','WP-10-T03','WP-10-T05'); tests = @('BenchmarkTest','CommonComponentsTest','ResponsiveListsTest'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'C-GATE' }
    'NFR-PERF-02' = @{ primary = 'WP-23'; supporting = @('WP-08'); tasks = @('WP-23-T03','WP-08-T03'); tests = @('BenchmarkTest','Canceled','Failed'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'C-GATE' }
    'NFR-PERF-03' = @{ primary = 'WP-23'; supporting = @('WP-10'); tasks = @('WP-23-T03','WP-10-T01','WP-10-T03','WP-10-T05','WP-10-T06'); tests = @('BenchmarkTest','WorkbenchShellGuiTest','ThreeScaleProfilesKeepPrimaryActionsVisible'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'C-GATE' }
    'NFR-PERF-04' = @{ primary = 'WP-23'; supporting = @('WP-08'); tasks = @('WP-23-T03','WP-08-T05','WP-21-T01','WP-21-T02','WP-21-T03','WP-21-T04','WP-21-T05','WP-21-T06'); tests = @('BenchmarkTest','BoundedParallelismTest','JointSearchTest'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'D-GATE' }
    'NFR-PERF-05' = @{ primary = 'WP-23'; supporting = @('WP-08'); tasks = @('WP-23-T03','WP-08-T05','WP-21-T01','WP-21-T02','WP-21-T03','WP-21-T04','WP-21-T05','WP-21-T06'); tests = @('BenchmarkTest','BoundedParallelismTest','JointSearchTest'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'D-GATE' }
    'NFR-PERF-06' = @{ primary = 'WP-23'; supporting = @('WP-08'); tasks = @('WP-23-T03','WP-08-T05','WP-21-T01','WP-21-T02','WP-21-T03','WP-21-T04','WP-21-T05','WP-21-T06'); tests = @('BenchmarkTest','BoundedParallelismTest','JointSearchTest'); evidence = 'out/test-evidence/wp-23/<run-id>/'; gate = 'D-GATE' }
    'NFR-REL-01' = @{ primary = 'WP-04'; supporting = @('WP-23'); tasks = @('WP-04-T01','WP-04-T02','WP-04-T03','WP-04-T04','WP-23-T01','WP-23-T05'); tests = @('PathSafetyTest','CommandRevisionTest','ProjectCommandContractTest'); evidence = 'out/test-evidence/wp-04/<run-id>/'; gate = 'A-GATE-04/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-REL-02' = @{ primary = 'WP-08'; supporting = @('WP-23'); tasks = @('WP-08-T01','WP-08-T02','WP-08-T03','WP-08-T04','WP-21-T03','WP-21-T06','WP-23-T01','WP-23-T02','WP-23-T05'); tests = @('StateMachineTest','SchedulerContractTest','RequestIdentityTest'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-04/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-REL-03' = @{ primary = 'WP-08'; supporting = @('WP-23'); tasks = @('WP-08-T01','WP-08-T03','WP-08-T04','WP-23-T01','WP-23-T05'); tests = @('StateMachineTest','SchedulerContractTest','Canceled'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-04/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-REL-04' = @{ primary = 'WP-11'; supporting = @('WP-04'); tasks = @('WP-11-T01','WP-11-T04','WP-04-T01','WP-04-T03','WP-04-T05','WP-23-T01','WP-23-T05'); tests = @('PathBudgetTest','CatalogVersion','CatalogImportTest'); evidence = 'out/test-evidence/wp-11/<run-id>/'; gate = 'A-GATE-04/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-REL-05' = @{ primary = 'WP-09'; supporting = @(); tasks = @('WP-09-T01','WP-09-T03','WP-09-T04','WP-23-T01','WP-23-T05'); tests = @('DiagnosticSchemaTest','ErrorMappingTest','RedactedLoggingTest'); evidence = 'out/test-evidence/wp-09/<run-id>/'; gate = 'A-GATE-04/B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-01' = @{ primary = 'WP-11'; supporting = @('WP-24'); tasks = @('WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05'); tests = @('PathBudgetTest','CsvReaderTest','CsvReader'); evidence = 'out/test-evidence/wp-11/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-02' = @{ primary = 'WP-11'; supporting = @('WP-24'); tasks = @('WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05'); tests = @('PathBudgetTest','CsvReaderTest','CsvReader'); evidence = 'out/test-evidence/wp-11/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-03' = @{ primary = 'WP-11'; supporting = @('WP-24'); tasks = @('WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05','WP-12-T03','WP-12-T04','WP-14-T02'); tests = @('PathBudgetTest','CsvReaderTest','CsvReader'); evidence = 'out/test-evidence/wp-11/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-04' = @{ primary = 'WP-01'; supporting = @('WP-24'); tasks = @('WP-24-T01','WP-24-T04'); tests = @('InstallerScriptTest'); evidence = 'out/test-evidence/wp-01/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-05' = @{ primary = 'WP-24'; supporting = @('WP-01'); tasks = @('WP-24-T01','WP-24-T02','WP-24-T04','WP-01-T05'); tests = @('InstallerScriptTest','package.ps1','verify-package.ps1'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'NFR-SEC-06' = @{ primary = 'WP-24'; supporting = @(); tasks = @('WP-24-T05'); tests = @('WP-24-T05 检查表首项'); evidence = 'out/test-evidence/wp-24/<run-id>/'; gate = 'R-GATE' }
    'NFR-SEC-07' = @{ primary = 'WP-09'; supporting = @('WP-24'); tasks = @('WP-09-T01','WP-09-T04','WP-09-T05','WP-12-T04','WP-12-T06'); tests = @('DiagnosticSchemaTest','RedactedLoggingTest','StaticConsistencyTest'); evidence = 'out/test-evidence/wp-09/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'OPT-01' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T01','WP-20-T02','WP-21-T01','WP-21-T06'); tests = @('StudyDefinitionTest','RejectsUnregisteredBinding','RejectsWriteSetOverlap'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-02' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T01','WP-20-T02','WP-21-T01','WP-21-T06'); tests = @('StudyDefinitionTest','RejectsUnregisteredBinding','RejectsWriteSetOverlap'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-03' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T03','WP-21-T02','WP-21-T06'); tests = @('StaticConstraintTest','MustViolationExcludedFromFeasibleSet','FixedEvaluationOrderNotReordered'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-04' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T04','WP-12-T02','WP-21-T02','WP-21-T04','WP-21-T06','WP-21-T07'); tests = @('StaticParetoTest','JointOptimizationGuiTest','CandidateTableHasEightMetrics'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-05' = @{ primary = 'WP-21'; supporting = @(); tasks = @('WP-21-T01','WP-21-T06'); tests = @('JointSearchTest','AcceptanceEvidenceTest'); evidence = 'out/test-evidence/wp-21/<run-id>/'; gate = 'D-GATE' }
    'OPT-06' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T05','WP-21-T01','WP-21-T02','WP-21-T03','WP-21-T04','WP-21-T06'); tests = @('CacheDeterminismTest','CacheKeyCoversAllFourFields','DependencyChangePreventsHit'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-07' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T04','WP-20-T06','WP-12-T04','WP-21-T02','WP-21-T04','WP-21-T06'); tests = @('StaticParetoTest','OnlyThreeStageBMetricsComputable','DominanceRequiresToleranceExceededStrictWin'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-08' = @{ primary = 'WP-20'; supporting = @('WP-21'); tasks = @('WP-20-T04','WP-20-T06','WP-20-T07','WP-12-T04','WP-12-T05','WP-21-T05','WP-21-T06','WP-21-T07'); tests = @('StaticParetoTest','JointOptimizationGuiTest','AdoptRequiresCurrentFormalReview'); evidence = 'out/test-evidence/wp-20/<run-id>/'; gate = 'B-GATE/D-GATE' }
    'OPT-09' = @{ primary = 'WP-21'; supporting = @(); tasks = @('WP-21-T04','WP-21-T06','WP-21-T07','WP-12-T04','WP-12-T05'); tests = @('ParetoRobustnessTest','JointOptimizationGuiTest','ParetoSelectionSynchronizesDetails'); evidence = 'out/test-evidence/wp-21/<run-id>/'; gate = 'D-GATE' }
    'OPT-10' = @{ primary = 'WP-21'; supporting = @(); tasks = @('WP-21-T01','WP-21-T06'); tests = @('JointSearchTest','AcceptanceEvidenceTest'); evidence = 'out/test-evidence/wp-21/<run-id>/'; gate = 'D-GATE' }
    'REQ-01' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01','WP-14-T03'); tests = @('tcpRef','RequirementsModelTest','validateBudget'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE/C-GATE' }
    'REQ-02' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01','WP-14-T04'); tests = @('tcpRef','RequirementsModelTest','payloadMass'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE/C-GATE' }
    'REQ-03' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01','WP-14-T03'); tests = @('tcpRef','RequirementsModelTest','validateBudget'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE/C-GATE' }
    'REQ-04' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01','WP-14-T04'); tests = @('tcpRef','RequirementsModelTest','payloadMass'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE/C-GATE' }
    'REQ-05' = @{ primary = 'WP-14'; supporting = @('WP-11'); tasks = @('WP-14-T01','WP-14-T02','WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05'); tests = @('tcpRef','RequirementsModelTest','CsvIoTest'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE' }
    'REQ-06' = @{ primary = 'WP-14'; supporting = @('WP-05','WP-12'); tasks = @('WP-14-T01','WP-14-T04','WP-14-T05','WP-14-T06','WP-14-T07','WP-12-T01','WP-12-T02','WP-12-T03','WP-12-T04','WP-12-T05','WP-12-T06'); tests = @('tcpRef','RequirementsModelTest','payloadMass'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE' }
    'REQ-07' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01','WP-14-T02','WP-14-T07'); tests = @('tcpRef','RequirementsModelTest','CsvIoTest'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE' }
    'REQ-08' = @{ primary = 'WP-14'; supporting = @(); tasks = @('WP-14-T01'); tests = @('tcpRef','RequirementsModelTest'); evidence = 'out/test-evidence/wp-14/<run-id>/'; gate = 'B-GATE' }
    'SEL-01' = @{ primary = 'WP-19'; supporting = @('WP-11'); tasks = @('WP-19-T01','WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05'); tests = @('CatalogSchemaTest','ViewMatchesColumnDictionary','UnitsAreSiDeclared'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-02' = @{ primary = 'WP-19'; supporting = @('WP-11'); tasks = @('WP-19-T01','WP-19-T02','WP-11-T01','WP-11-T02','WP-11-T03','WP-11-T04','WP-11-T05'); tests = @('CatalogSchemaTest','ViewMatchesColumnDictionary','UnitsAreSiDeclared'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-03' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T03'); tests = @('ConstraintFilterTest','HardEliminationBeforeMargin','EveryEliminationCarriesCodeValueThreshold'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-04' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T03'); tests = @('ConstraintFilterTest','HardEliminationBeforeMargin','EveryEliminationCarriesCodeValueThreshold'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-05' = @{ primary = 'WP-19'; supporting = @('WP-18'); tasks = @('WP-19-T04','WP-18-T01','WP-18-T02','WP-18-T03','WP-18-T04','WP-18-T05'); tests = @('MappingCheckTest','UsesSharedEvaluatorForOperatingPoint','InertiaRatioDefaultSoftConstraint'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-06' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T03','WP-19-T05','WP-19-T07'); tests = @('ConstraintFilterTest','SelectionGuiTest','RejectedRowsExposeReasonAndMargin'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-07' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T03'); tests = @('ConstraintFilterTest','HardEliminationBeforeMargin','EveryEliminationCarriesCodeValueThreshold'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-08' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T06','WP-19-T07'); tests = @('CatalogVersionTest','SelectionGuiTest','CatalogVersionIsAlwaysVisible'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'SEL-09' = @{ primary = 'WP-19'; supporting = @(); tasks = @('WP-19-T04'); tests = @('MappingCheckTest','UsesSharedEvaluatorForOperatingPoint','InertiaRatioDefaultSoftConstraint'); evidence = 'out/test-evidence/wp-19/<run-id>/'; gate = 'C-GATE' }
    'TASK-01' = @{ primary = 'WP-08'; supporting = @(); tasks = @('WP-08-T01','WP-08-T02','WP-08-T03','WP-21-T03','WP-21-T06','WP-23-T02'); tests = @('StateMachineTest','SchedulerContractTest','RequestIdentityTest'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-05' }
    'TASK-02' = @{ primary = 'WP-08'; supporting = @(); tasks = @('WP-08-T01','WP-08-T02','WP-08-T03','WP-21-T06'); tests = @('StateMachineTest','SchedulerContractTest','RequestIdentityTest'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-01～07' }
    'TASK-03' = @{ primary = 'WP-08'; supporting = @(); tasks = @('WP-08-T01','WP-08-T02','WP-08-T03','WP-21-T06','WP-23-T02'); tests = @('StateMachineTest','SchedulerContractTest','RequestIdentityTest'); evidence = 'out/test-evidence/wp-08/<run-id>/'; gate = 'A-GATE-03' }
    'TRJ-01' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T01','WP-16-T06'); tests = @('PtpSegmentFromOrderedTaskSequence','CartesianLineApproachRetreatSegments','DwellSegmentCarriesDurationAndLoadCaseRef'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-02' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T01','WP-16-T06'); tests = @('PtpSegmentFromOrderedTaskSequence','CartesianLineApproachRetreatSegments','DwellSegmentCarriesDurationAndLoadCaseRef'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-03' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T02','WP-16-T06'); tests = @('PlannerVersionParamsSeedRecordedInSnapshot','ConstraintConstructionOnlyViaWp07Projection','NoPathReportsSegmentAndEndpoints'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-04' = @{ primary = 'WP-16'; supporting = @('WP-07'); tasks = @('WP-16-T03','WP-16-T04','WP-16-T06','WP-07-T01','WP-07-T02','WP-07-T03'); tests = @('SimplifierRemovesZeroDisplacementAndCollinearWaypoints','SimplifierKeepsTaskPointSemantics','QuinticSplineMatchesC2AtKnots'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-05' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T03','WP-16-T06'); tests = @('SimplifierRemovesZeroDisplacementAndCollinearWaypoints','SimplifierKeepsTaskPointSemantics','QuinticSplineMatchesC2AtKnots'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-06' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T05','WP-16-T06'); tests = @('ResolvedIkBranchSequenceRecordsAdoptedSolutions','TrajectoryPlanPayloadCompletePerSchemaV1','EnvelopePassesLegalCombinationValidation'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-07' = @{ primary = 'WP-16'; supporting = @('WP-10'); tasks = @('WP-16-T05','WP-16-T07','WP-10-T02'); tests = @('TrajectoryGuiTest','SegmentTableColumnsMatchSpecification','CurveSelectionSyncsSceneWithoutCommit'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'TRJ-08' = @{ primary = 'WP-16'; supporting = @(); tasks = @('WP-16-T01'); tests = @('PtpSegmentFromOrderedTaskSequence','CartesianLineApproachRetreatSegments','DwellSegmentCarriesDurationAndLoadCaseRef'); evidence = 'out/test-evidence/wp-16/<run-id>/'; gate = 'C-GATE' }
    'UX-01' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T03','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T01','WP-22-T02','WP-22-T04','WP-22-T05','WP-22-T06','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest','ProjectEntryModelTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-02' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T03','WP-10-T05','WP-10-T06','WP-09-T02','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T04','WP-22-T05','WP-22-T06','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest','ProjectEntryModelTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-03' = @{ primary = 'WP-09'; supporting = @('WP-10','WP-22'); tasks = @('WP-09-T01','WP-09-T02','WP-09-T03','WP-10-T01','WP-10-T02','WP-10-T03','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T02','WP-22-T04','WP-22-T05','WP-22-T06','WP-25-T03'); tests = @('DiagnosticSchemaTest','WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest','ProjectEntryModelTest'); evidence = 'out/test-evidence/wp-09/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-04' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T03','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T05','WP-22-T06','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest','ProjectEntryModelTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-05' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T03','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T05','WP-22-T06','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest','ProjectEntryModelTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-06' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T05','WP-10-T06','WP-09-T02','WP-09-T04','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T01','WP-22-T02','WP-22-T03','WP-22-T05','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-07' = @{ primary = 'WP-10'; supporting = @('WP-22'); tasks = @('WP-10-T01','WP-10-T02','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T05','WP-25-T03'); tests = @('WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest'); evidence = 'out/test-evidence/wp-10/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'UX-08' = @{ primary = 'WP-07'; supporting = @('WP-10','WP-22'); tasks = @('WP-07-T01','WP-10-T01','WP-10-T02','WP-10-T04','WP-10-T06','WP-13-T08','WP-14-T07','WP-16-T07','WP-17-T07','WP-19-T07','WP-20-T07','WP-21-T07','WP-22-T05','WP-25-T03'); tests = @('PolicyNormalizationTest','WorkbenchShellGuiTest','TrajectoryGuiTest','DynamicsGuiTest','SelectionGuiTest','JointOptimizationGuiTest'); evidence = 'out/test-evidence/wp-07/<run-id>/'; gate = 'B-GATE/C-GATE/D-GATE/R-GATE' }
    'PILOT-01' = @{ primary = 'WP-25'; supporting = @('WP-00'); tasks = @('WP-25-T01'); tests = @('pilot-data-signoff-review'); evidence = 'out/test-evidence/wp-25/<run-id>/'; gate = 'R-GATE' }
    'PILOT-02' = @{ primary = 'WP-25'; supporting = @('WP-24'); tasks = @('WP-25-T02'); tests = @('comparison-report-review'); evidence = 'out/test-evidence/wp-25/<run-id>/'; gate = 'R-GATE' }
    'DEL-01' = @{ primary = 'WP-25'; supporting = @('WP-24'); tasks = @('WP-25-T05'); tests = @('release-checklist-review'); evidence = 'out/test-evidence/wp-25/<run-id>/'; gate = 'R-GATE' }
    'DEL-02' = @{ primary = 'WP-25'; supporting = @('WP-00'); tasks = @('WP-25-T04'); tests = @('defect-register-review'); evidence = 'out/test-evidence/wp-25/<run-id>/'; gate = 'R-GATE' }
}

if (-not (Test-Path -LiteralPath $RequirementsPath)) {
    throw "Requirements document not found: $RequirementsPath"
}

$text = Get-Content -LiteralPath $RequirementsPath -Raw -Encoding UTF8
$requirementMatches = [regex]::Matches(
    $text,
    '(?m)^\|\s*([A-Z][A-Z0-9-]*-\d+)\s*\|\s*(P[01])\s*\|\s*(.*?)\s*\|\s*$'
)

$reqRows = [ordered]@{}
foreach ($match in $requirementMatches) {
    $id = $match.Groups[1].Value
    if ($reqRows.Contains($id)) {
        throw "Duplicate requirement row in requirements document: $id"
    }
    $reqRows[$id] = [pscustomobject]@{
        Priority = $match.Groups[2].Value
        Summary  = $match.Groups[3].Value.Trim()
    }
}

$traceStart = $text.IndexOf('## 16. 需求—验收追踪')
if ($traceStart -lt 0) {
    throw 'Could not locate the requirement traceability section: missing anchor "## 16. 需求—验收追踪".'
}
$traceEnd = $text.IndexOf('## 17.', $traceStart)
if ($traceEnd -lt 0) {
    throw 'Could not locate the requirement traceability section: missing anchor "## 17.".'
}

$traceText = $text.Substring($traceStart, $traceEnd - $traceStart)
$acceptanceById = @{}

foreach ($line in ($traceText -split "`r?`n")) {
    if ($line -notmatch '^\|') {
        continue
    }

    $cells = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    if ($cells.Count -ne 4 -or $cells[0] -eq '需求 ID' -or $cells[0] -match '^---') {
        continue
    }

    foreach ($id in (Expand-RequirementCell -Cell $cells[0])) {
        $entry = [pscustomobject]@{
            Method = $cells[1]
            Scenario = $cells[2]
            Phase = $cells[3]
        }

        if (-not $acceptanceById.ContainsKey($id)) {
            $acceptanceById[$id] = [System.Collections.Generic.List[object]]::new()
        }
        $acceptanceById[$id].Add($entry)
    }
}

# 映射表与需求文档必须一一对应（数量由需求文档解析结果派生）。
if ($reqRows.Count -ne $requirementMap.Count) {
    throw ("Mapping table size {0} does not match requirement rows {1}." -f $requirementMap.Count, $reqRows.Count)
}
foreach ($id in $reqRows.Keys) {
    if (-not $requirementMap.ContainsKey($id)) {
        throw "No mapping entry for requirement $id."
    }
}
foreach ($id in $requirementMap.Keys) {
    if (-not $reqRows.Contains($id)) {
        throw "Mapping entry $id does not exist in the requirements document."
    }
}

# 映射表中的每个任务 ID 都必须有真实任务卡（agent-tasks/WP-XX-TYY-*.md）。
$agentTaskPath = Join-Path $PSScriptRoot 'agent-tasks'
$realTaskIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($cardFile in (Get-ChildItem -LiteralPath $agentTaskPath -File -Filter 'WP-*.md')) {
    if ($cardFile.Name -match '^((WP-\d{2})-T\d{2})-') {
        [void]$realTaskIds.Add($Matches[1])
    }
}
foreach ($entry in $requirementMap.Values) {
    foreach ($taskId in $entry.tasks) {
        if (-not $realTaskIds.Contains($taskId)) {
            throw "Mapped task $taskId (requirement $($entry.primary)) has no task card in agent-tasks/."
        }
    }
    if (@($entry.tasks).Count -lt 1) {
        throw "Mapping entry with primary $($entry.primary) has no agent task."
    }
    if (@($entry.tests).Count -lt 1) {
        throw "Mapping entry with primary $($entry.primary) has no test case."
    }
    foreach ($field in @('primary', 'evidence', 'gate')) {
        if ([string]::IsNullOrWhiteSpace($entry.$field)) {
            throw "Mapping entry with primary $($entry.primary) has an empty $field."
        }
    }
}

# 确定性输出顺序：需求 ID 的 Ordinal 排序（PS 5.1 / 7 一致）。
$sortedIds = [System.Collections.Generic.List[string]]::new()
foreach ($id in $reqRows.Keys) {
    $sortedIds.Add($id)
}
$sortedIds.Sort([System.StringComparer]::Ordinal)

$rows = foreach ($id in $sortedIds) {
    $req = $reqRows[$id]
    $map = Get-WorkPackages -RequirementId $id
    $acceptanceEntries = @($acceptanceById[$id])

    if ($acceptanceEntries.Count -eq 0) {
        throw "Requirement $id has no acceptance trace entry."
    }

    $methods = @($acceptanceEntries | ForEach-Object { $_.Method } | Select-Object -Unique)
    $scenarios = @($acceptanceEntries | ForEach-Object { $_.Scenario } | Select-Object -Unique)
    $phases = @($acceptanceEntries | ForEach-Object { $_.Phase } | Select-Object -Unique)
    $scenarioText = (($methods -join ' / ') + '；' + ($scenarios -join ' / '))
    if ($scenarioText.Length -gt 120) {
        $scenarioText = $scenarioText.Substring(0, 120) + '…'
    }

    $supportingText = @($map.supporting) -join ';'
    if ([string]::IsNullOrWhiteSpace($supportingText)) {
        $supportingText = '-'
    }

    [pscustomobject][ordered]@{
        requirement_id = $id
        priority = $req.Priority
        requirement_summary = $req.Summary
        primary_wp = $map.primary
        supporting_wps = $supportingText
        agent_task_ids = @($map.tasks) -join ';'
        test_case_ids = @($map.tests) -join ';'
        acceptance_scenario = $scenarioText
        evidence_artifact = $map.evidence
        release_gate = $map.gate
        phase = $phases -join '/'
        release = Get-Release -RequirementId $id
        status = 'Planned'
    }
}

$duplicateIds = @($rows | Group-Object requirement_id | Where-Object Count -ne 1)
if ($duplicateIds.Count -gt 0) {
    throw "Duplicate requirement rows: $($duplicateIds.Name -join ', ')"
}
if ($rows.Count -ne $reqRows.Count) {
    throw ("Expected {0} requirement rows, found {1}." -f $reqRows.Count, $rows.Count)
}

# 导出为确定的 UTF-8 BOM + CRLF 字节，兼容 Windows PowerShell 5.1 与 PowerShell 7：
# Export-Csv 的 -Encoding 取值在两个版本间不同（utf8BOM 仅 PS7 支持），
# 因此先导出到临时文件，再统一换行并以显式编码写回。
$tempCsv = Join-Path ([System.IO.Path]::GetTempPath()) ("ird-trace-gen-" + [guid]::NewGuid() + '.csv')
try {
    $rows | Export-Csv -LiteralPath $tempCsv -NoTypeInformation -Encoding UTF8
    $csvText = [System.IO.File]::ReadAllText($tempCsv)
    $csvText = ($csvText -replace "`r`n", "`n") -replace "`n", "`r`n"
    Write-Utf8BomCrlfAtomically -Path $OutputPath -Text $csvText
}
finally {
    if (Test-Path -LiteralPath $tempCsv) {
        Remove-Item -LiteralPath $tempCsv -Force
    }
}

# ---------------------------------------------------------------- 治理追踪（D12）
# 治理任务（WP-00 等）不对应产品需求 ID，单独登记为 GovernanceExempt；
# 验证器要求每张任务卡要么出现在需求追踪表，要么出现在本表。
$governanceMap = @(
    @{ task = 'WP-00-T01'; wp = 'WP-00'; purpose = '冻结需求基线版本与基线提交（DOCUMENT-BASELINE §3）'; gate = 'IRD-D0-20260829' }
    @{ task = 'WP-00-T02'; wp = 'WP-00'; purpose = '生成并校验需求—任务—测试追踪矩阵与反向追踪'; gate = 'IRD-D8-20260829' }
    @{ task = 'WP-00-T03'; wp = 'WP-00'; purpose = '文档验证门禁脚本、Schema 门禁与任务状态账本维护'; gate = 'IRD-D3/D9/D12-20260829' }
    @{ task = 'WP-00-T04'; wp = 'WP-00'; purpose = '独立评审与契约联合评审记录归档'; gate = 'IRD-D10-20260829' }
)

$governanceRows = foreach ($g in $governanceMap) {
    [pscustomobject][ordered]@{
        task_id = $g.task
        wp = $g.wp
        trace_kind = 'GovernanceExempt'
        purpose = $g.purpose
        gate_or_checkpoint = $g.gate
        artifact = 'out/test-evidence/wp-00/<run-id>/'
    }
}

$outputDir = Split-Path -Parent $OutputPath
if ([string]::IsNullOrWhiteSpace($outputDir)) { $outputDir = $PSScriptRoot }
$governancePath = Join-Path $outputDir 'governance-traceability.csv'
$tempGov = Join-Path ([System.IO.Path]::GetTempPath()) ("ird-gov-gen-" + [guid]::NewGuid() + '.csv')
try {
    $governanceRows | Export-Csv -LiteralPath $tempGov -NoTypeInformation -Encoding UTF8
    $govText = [System.IO.File]::ReadAllText($tempGov)
    $govText = ($govText -replace "`r`n", "`n") -replace "`n", "`r`n"
    Write-Utf8BomCrlfAtomically -Path $governancePath -Text $govText
}
finally {
    if (Test-Path -LiteralPath $tempGov) {
        Remove-Item -LiteralPath $tempGov -Force
    }
}

Write-Output "Generated $($rows.Count) traceability rows at $OutputPath"
