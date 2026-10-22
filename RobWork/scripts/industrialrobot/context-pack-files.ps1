# 上下文包的源文件清单：从原生成器提取，供生成与验证共同使用。
# 只列已有契约/规则输入，不计算摘要，不引入第二套上下文或依赖解析机制。
function Get-ContextFingerprintFiles([object]$Contract, [string]$ContractPath, [string]$RepoTop) {
    $files = New-Object System.Collections.Generic.List[string]
    $relative = [IO.Path]::GetFullPath($ContractPath).Substring([IO.Path]::GetFullPath($RepoTop).Length + 1).Replace('\','/')
    $files.Add($relative)
    foreach ($reference in @($Contract.designRefs)) {
        if (-not $reference) { continue }
        # 锚点约束由任务门禁承担；指纹绑定该章节所属源文件，沿用既有路径语义。
        $relative = 'RobWork/doc/industrial-robot-design/' + (($reference -replace '#.*$','') -replace '^\./','').Replace('\','/')
        if ($files -notcontains $relative) { $files.Add($relative) }
    }
    foreach ($extra in @('RobWork/doc/industrial-robot-design/REQUIREMENTS.md','RobWork/doc/industrial-robot-design/context/reading-paths.md','RobWork/doc/industrial-robot-design/context/asset-inventory.json','AGENTS.md','RobWork/doc/industrial-robot-design/AGENTS.md','RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md')) {
        if ($files -notcontains $extra) { $files.Add($extra) }
    }
    return $files.ToArray()
}
