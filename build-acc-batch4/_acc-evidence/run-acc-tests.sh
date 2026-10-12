#!/bin/bash
# 验收者全量测试跑批：$1=模式名(integration/smoke)，$2=exe根目录模式，$3=XML输出目录
MODE="$1"; BINDIR="$2"; XMLDIR="$3"
mkdir -p "$XMLDIR"
export PATH="/d/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin:/d/software/Qt/6.11.1/msvc2022_64/bin:/d/software/miniconda3:$BINDIR:$PATH"
TOTAL_FAIL=0
: > "$XMLDIR/exitcodes.txt"
for exe in $(find "$BINDIR" -name "sdurws_ird_*test.exe" -o -name "sdurws_ird_*_contract_test.exe" -o -name "sdurws_ird_*_gui_test.exe" | sort); do
  name=$(basename "$exe" .exe)
  "$exe" --gtest_output="xml:$XMLDIR/${MODE}-${name}.xml" > "$XMLDIR/${MODE}-${name}-run.log" 2>&1
  rc=$?
  echo "$name EXIT=$rc" >> "$XMLDIR/exitcodes.txt"
  if [ $rc -ne 0 ]; then TOTAL_FAIL=$((TOTAL_FAIL+1)); echo "FAILED: $name (exit=$rc)"; fi
done
echo "SUITES_WITH_NONZERO_EXIT=$TOTAL_FAIL"
