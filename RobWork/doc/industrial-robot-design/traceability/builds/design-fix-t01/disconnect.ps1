$ErrorActionPreference='Stop'
$repo='D:/10_Source_Repos/21_robot/RobWork'
$evidenceDir="$repo/RobWork/doc/industrial-robot-design/traceability/builds/design-fix-t01"
$source="$repo/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/MappingCore.cpp"
$originalBytes=[IO.File]::ReadAllBytes($source)
$originalText=[IO.File]::ReadAllText($source)
[IO.File]::WriteAllBytes("$repo/build/design-fix-t01/MappingCore.original.cpp",$originalBytes)
cmd /d /s /c '"D:\software\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && set' | ForEach-Object {
 if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}
}
$env:PATH="$repo/vcpkg/installed/x64-windows/bin;D:/software/QT/6.11.1/msvc2022_64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='windows'
$exe="$repo/build/RobWorkStudio/bin/Release/sdurws_ird_drivetrain_test.exe"
try {
 foreach($kind in @('singular','inverse')) {
  $mutated=if($kind -eq 'singular'){$originalText.Replace('rw::math::LinearAlgebra::svd(m, u, sigma, v);','rw::math::LinearAlgebra::svd(m, u, sigma, v);'+"`r`n"+'    sigma = Eigen::VectorXd::Constant(sigma.size(), 1.0); // DESIGN-T01 DISCONNECT: 丢弃上游结果')}else{$originalText.Replace('const Eigen::MatrixXd inv = rw::math::LinearAlgebra::inverse(m);','const Eigen::MatrixXd inv = rw::math::LinearAlgebra::inverse(m) * 1e300; // DESIGN-T01 DISCONNECT')}
  if($mutated -eq $originalText){throw 'mutation anchor not found'}
  [IO.File]::WriteAllText($source,$mutated,[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText("$evidenceDir/MappingCore-$kind.mutated.cpp",$mutated,[Text.UTF8Encoding]::new($false))
  & cmake --build "$repo/build" --config Release --target sdurws_ird_drivetrain_test -- /m:4 *> "$repo/build/design-fix-t01/disconnect-$kind-build.log"
  if($LASTEXITCODE){throw "mutation build failed: $kind"}
  $filter=if($kind -eq 'singular'){'DtMappingR2Linalg.SingularValuesAnalyticSpectrum_ConditionNumber'}else{'DtMappingR2Linalg.InverseAnalyticOracle_WindowConsumers'}
  & $exe "--gtest_filter=$filter" "--gtest_output=xml:$evidenceDir/disconnect-$kind.xml" "--ird_report=$evidenceDir/disconnect-$kind-report.json" *> "$evidenceDir/disconnect-$kind.log"
  $result=$LASTEXITCODE
  "${kind}: exit=$result (expected assertion failure)"
  if($result -ne 1){throw "mutation did not fail an assertion as expected: $kind exit=$result"}
  [IO.File]::WriteAllBytes($source,$originalBytes)
 }
} finally {
 [IO.File]::WriteAllBytes($source,$originalBytes)
 & cmake --build "$repo/build" --config Release --target sdurws_ird_drivetrain_test -- /m:4 *> "$repo/build/design-fix-t01/restore-build.log"
 if($LASTEXITCODE){throw 'restored build failed'}
 & $exe "--gtest_output=xml:$repo/build/design-fix-t01/drivetrain-final.xml" "--ird_report=$repo/build/design-fix-t01/drivetrain-final-report.json" *> "$repo/build/design-fix-t01/drivetrain-final.log"
 "restored: exit=$LASTEXITCODE"
 if($LASTEXITCODE){throw 'restored tests failed'}
}
