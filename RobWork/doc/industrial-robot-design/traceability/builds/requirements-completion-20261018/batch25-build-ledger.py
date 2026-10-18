"""WP-14-T12：从不可变审计和真实 XML 建立逐项终验工作表。

组件通过只登记为组件证据，不能推断人机交互或三维显示已通过。
本脚本不修改原审计、原测试报告或流水线状态。
"""

import hashlib
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def read_json(path):
    """以 UTF-8 读取原始报告；不修写源文件。"""
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    """核对完整 ID 集，再输出一次性台账及便于人工逐行审阅的表格。"""
    base = Path(__file__).resolve().parent
    audit_path = base.parent / "requirements-audit-20261018/functional-matrix.json"
    audit = read_json(audit_path)
    summary = read_json(base / "batch24-verification-summary.json")
    tests = []
    for executable, record in summary["integrated"]["executables"].items():
        for case in ET.parse(base / record["evidence"]).getroot().iter("testcase"):
            status = "FAIL" if case.find("failure") is not None else (
                "SKIP" if case.find("skipped") is not None
                or case.get("status") == "notrun" else "PASS")
            tests.append({"name": case.get("classname") + "." + case.get("name"),
                          "status": status, "executable": executable,
                          "evidence": record["evidence"]})

    red_evidence = "batch25-red-ui/sdurws_ird_ui_test/gtest.xml"
    red_cases = {case.get("name"): case for case in
                 ET.parse(base / red_evidence).getroot().iter("testcase")}
    mask_name = "BatchHonorsEveryConstraintMask_REQ01_A16"
    rule_name = "BatchDirectionRulesDoNotFallBackToFixed_REQ09_A16"
    assert red_cases[mask_name].find("failure") is not None
    assert red_cases[rule_name].find("failure") is not None
    failures = {
        "R01-09": (mask_name, "六个单分量目标本应零位满足，却被自由坐标99m错误拒绝。"),
        "R09-03": (rule_name, "真实Frame局部+Z指向世界-Y，平面机器人工具+Z无法对齐却被判成功。"),
        "R09-04": (rule_name, "工具+Z无法指向平面内目标方向，当前消费却判成功。"),
        "R09-05": (rule_name, "允许绕工具+Z滚转的独立黄金构型被错误判失败。"),
    }
    rows = []
    for original in audit["items"]:
        filters = original["testFilters"]
        old_names = {item["name"] for item in original["tests"]}
        component_tests = [test for test in tests if test["name"] in old_names
                           or any(value.lower() in test["name"].lower()
                                  for value in filters)]
        failure = failures.get(original["id"])
        rows.append({
            "id": original["id"], "basis": original["basis"],
            "feature": original["feature"], "method": original["method"],
            "historicalAudit": {"status": original["productStatus"],
                                "observation": original["observation"],
                                "evidence": original["evidence"]},
            "latestIntegratedComponentTests": component_tests,
            "finalVerificationStatus": "FAIL" if failure else "NOT RUN",
            "finalVerificationObservation": failure[1] if failure else (
                "尚未在当前版本对本行完成统一逐项终验；历史状态及最新组件证据仅供定位。"),
            "newFailureEvidence": [{"test": "KinEvaluationChannel." + failure[0],
                                    "evidence": red_evidence}] if failure else [],
            "remainingVerification": original["method"] + (
                "；按A-16修复后重跑真实求解，随后核对表单、三维方向和结果。"
                if failure else "；核对当前生产交互/权威值及相关三维或下游结果，附新证据后逐行定论。"),
        })

    ids = [row["id"] for row in rows]
    assert len(ids) == len(set(ids)) == 120
    assert set(ids) == {row["id"] for row in audit["items"]}
    output = {
        "schemaVersion": 1, "task": "WP-14-T12", "batch": 25,
        "baselineCommit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], text=True).strip(),
        "scope": "120项逐行终验工作表；不把历史审计或组件通过自动升级为产品终验通过。",
        "auditSource": "../requirements-audit-20261018/functional-matrix.json",
        "auditSha256": hashlib.sha256(audit_path.read_bytes()).hexdigest(),
        "latestComponentSummary": "batch24-verification-summary.json",
        "latestHostSummary": "batch24-host-summary.json",
        "summary": {"items": 120, "passed": 0, "failed": len(failures),
                    "skipped": 0, "notRun": len(rows) - len(failures),
                    "meaning": "仅统计统一逐行终验状态；不是既有组件/宿主通过数量。"},
        "items": rows,
    }
    json_path = base / "batch25-functional-ledger.json"
    md_path = base / "batch25-functional-ledger.md"
    if json_path.exists() or md_path.exists():
        raise RuntimeError("证据只增：目标已存在，须使用新批次文件名。")
    json_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    lines = ["# WP-14-T12：120项逐行终验工作表（第二十五批）", "",
             "本表完整保留原120个ID、功能及验证方法。历史审计状态只作追溯；"
             "最新组件XML匹配保存在同名JSON中，不据此宣称真实交互或三维终验通过。", "",
             "当前新增真实失败涉及4行、2个测试。其余116行尚未形成当前版本统一逐行终验结论，"
             "不表示此前已执行的组件和宿主证据无效。", "",
             "A-16公共设计尚待所有者审阅；另有区域TCP、任意旋转Box、真实mesh/法向、"
             "覆盖颜色/蓝圈及TRJ/DYN/选型联合流程待收口。", "",
             "| ID | 功能 | 如何验证 | 本次终验 | 最新组件匹配数 |",
             "| --- | --- | --- | --- | --- |"]
    for row in rows:
        cells = [row["id"], row["feature"], row["method"],
                 row["finalVerificationStatus"],
                 str(len(row["latestIntegratedComponentTests"]))]
        lines.append("| " + " | ".join(cell.replace("|", "\\|") for cell in cells) + " |")
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(output["summary"], ensure_ascii=False))
    print("ID完整性：120项，无遗漏、无重复；原始审计文件未改。")


if __name__ == "__main__":
    main()
