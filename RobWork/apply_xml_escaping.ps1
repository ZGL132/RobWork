# Apply XML-escaping substitutions to the writer source.
# Use PowerShell -replace (which is regex) but escape backslashes properly.
$ErrorActionPreference = 'Stop'
$path = 'D:\10_Source_Repos\21_robot\RobWork\RobWork\RobWorkStudio\src\rwslibs\robotmodelbuilder\RobotModelXmlWriter.cpp'
$src  = [System.IO.File]::ReadAllText($path, [System.Text.UTF8Encoding]::new($false))

# Each tuple: (pattern, replacement).  Use single-line mode (?n not needed; we
# avoid multi-line matches by keeping each pattern on a single line of source).
$replacements = @(
    # makeDynamicWorkCellXml block
    @('out << "<DynamicWorkCell workcell=\\"<< robotName << "Scene\.wc\.xml\\">\\n";',
      'out << "<DynamicWorkCell workcell=\\"<< xmlEscaped (robotName + "Scene.wc.xml") << "\\">\\n";'),
    @('out << "  <RigidDevice device=\\"<< robotName << "\\">\\n";',
      'out << "  <RigidDevice device=\\"<< xmlEscaped (robotName) << "\\">\\n";'),
    @('out << "    <ForceLimit joint=\\"<< QString::fromStdString \(fl\.jointName\) << "\\">"',
      'out << "    <ForceLimit joint=\\"<< xmlEscaped (fl.jointName) << "\\">"'),
    @('out << "    <KinematicBase frame=\\"<< QString::fromStdString \(spec\.dynamics\.baseFrame\) << "\\">\\n";',
      'out << "    <KinematicBase frame=\\"<< xmlEscaped (spec.dynamics.baseFrame) << "\\">\\n";'),
    @('out << "      <MaterialID>" << QString::fromStdString \(spec\.dynamics\.baseMaterial\) << "</MaterialID>\\n";',
      'out << "      <MaterialID>" << xmlEscaped (spec.dynamics.baseMaterial) << "</MaterialID>\\n";'),
    @('out << "    <Link object=\\"<< QString::fromStdString \(link\.objectName\) << "\\">\\n";',
      'out << "    <Link object=\\"<< xmlEscaped (link.objectName) << "\\">\\n";'),
    @('out << "      <MaterialID>" << QString::fromStdString \(link\.material\) << "</MaterialID>\\n";',
      'out << "      <MaterialID>" << xmlEscaped (link.material) << "</MaterialID>\\n";'),
    @('out << "    <FramePair first=\\"<< QString::fromStdString \(pair\.first\) << "\\" second=\\"<< QString::fromStdString \(pair\.second\) << "\\"/>\\n";',
      'out << "    <FramePair first=\\"<< xmlEscaped (pair.first) << "\\" second=\\"<< xmlEscaped (pair.second) << "\\"/>\\n";'),
    @('out << "  <Volatile>" << QString::fromStdString \(frame\) << "</Volatile>\\n";',
      'out << "  <Volatile>" << xmlEscaped (frame) << "</Volatile>\\n";'),
    @('out << "  <" << \(rule\.kind == ProximityRuleKind::Include \? "Include" : "Exclude"\) << " PatternA=\\"<< QString::fromStdString \(rule\.patternA\) << "\\" PatternB=\\"<< QString::fromStdString \(rule\.patternB\) << "\\"/>\\n";',
      'out << "  <" << (rule.kind == ProximityRuleKind::Include ? "Include" : "Exclude") << " PatternA=\\"<< xmlEscaped (rule.patternA) << "\\" PatternB=\\"<< xmlEscaped (rule.patternB) << "\\"/>\\n";'),
    @('out << "  <Frame name=\\"<< QString::fromStdString \(frame\.name\) << "\\" refframe=\\"<< QString::fromStdString \(frame\.refFrame\) << "\\"<< frameTypeAttribute\(frame\.frameType\);',
      'out << "  <Frame name=\\"<< xmlEscaped (frame.name) << "\\" refframe=\\"<< xmlEscaped (frame.refFrame) << "\\"<< frameTypeAttribute (frame.frameType);'),
    @('return QString \("<STL file=\\"%1\\" />"\)\.arg \(QString::fromStdString \(geometry\.file\)\);',
      'return QString ("<STL file=\"%1\" />").arg (xmlEscaped (geometry.file));'),
    @('return QString \("<Mesh file=\\"%1\\" />"\)\.arg \(QString::fromStdString \(geometry\.file\)\);',
      'return QString ("<Mesh file=\"%1\" />").arg (xmlEscaped (geometry.file));'),
    @('return QString \("<Polytope file=\\"%1\\" />"\)\.arg \(QString::fromStdString \(geometry\.file\)\);',
      'return QString ("<Polytope file=\"%1\" />").arg (xmlEscaped (geometry.file));'),
    @('out << "  <Drawable name=\\"<< QString::fromStdString \(geometry\.name\) << "\\" refframe=\\"<< QString::fromStdString \(geometry\.refFrame\) << "\\""',
      'out << "  <Drawable name=\\"<< xmlEscaped (geometry.name) << "\\" refframe=\\"<< xmlEscaped (geometry.refFrame) << "\\""'),
    @('out << "  <Drawable name=\\"<< QString::fromStdString \(drawable\.name\) << "\\" refframe=\\"<< QString::fromStdString \(drawable\.refFrame\) << "\\""',
      'out << "  <Drawable name=\\"<< xmlEscaped (drawable.name) << "\\" refframe=\\"<< xmlEscaped (drawable.refFrame) << "\\""'),
    @('return QString \("<Polytope file=\\"%1\\" />"\)\.arg \(relativeGeometryPath \(spec, drawable\.filePath\)\);',
      'return QString ("<Polytope file=\"%1\" />").arg (xmlEscaped (relativeGeometryPath (spec, drawable.filePath)));'),
    @('out << "  <CollisionModel name=\\"<< QString::fromStdString \(collision\.name\) << "\\" refframe=\\"<< QString::fromStdString \(collision\.refFrame\) << "\\">\\n";',
      'out << "  <CollisionModel name=\\"<< xmlEscaped (collision.name) << "\\" refframe=\\"<< xmlEscaped (collision.refFrame) << "\\">\\n";'),
    @('return QString \("<Polytope file=\\"%1\\" />"\)\.arg \(relativeGeometryPath \(spec, collision\.filePath\)\);',
      'return QString ("<Polytope file=\"%1\" />").arg (xmlEscaped (relativeGeometryPath (spec, collision.filePath)));'),
)

$total = 0
foreach ($pair in $replacements) {
    $pat = $pair[0]
    $rep = $pair[1]
    $count = ([regex]::Matches($src, $pat)).Count
    if ($count -gt 0) {
        $src = [regex]::Replace($src, $pat, $rep)
        $total += $count
        Write-Host "  applied ($count): $($pat.Substring(0, [Math]::Min(70, $pat.Length)))..."
    }
}

[System.IO.File]::WriteAllText($path, $src, [System.Text.UTF8Encoding]::new($false))
Write-Host "Total substitutions: $total"
