// =====================================================================
// make_namemap.mjs —— rt-namemap-roundtrip 数据集生成脚本（contract-fixture，
// AT-18 双向往返载体；units/runtime.md §7.1 范围表/§7.2 生成规则/§12 RT-T12）。
//
// 用法：
//   node make_namemap.mjs            （脚本位于 <版本目录>/generate/ 下）
//
// 生成逻辑：从 inputs/robot.json 的 robotLocalName 与对象集合，按
// units/runtime.md §7.1 名称范围表派生期望条目集（身份作用域＋派生作用域）：
//   robot → Device（设备名本身，无前缀）；joint_i → Joint；
//   link[0] → LinkFrame ＋ BaseFrame（设备基座帧）；link[N] → LinkFrame ＋
//   Flange（设备末端帧）；robot → BaseMount（§6.3 安装帧）。
// 本夹具无工具/场景/资源/DWC（物性缺失→无 Body 条目），条目集即上表。
// 全名形态（§7.3）：Device 条目 fullName==localName；其余
//   fullName = robotLocalName + "." + localName。
// 期望值由脚本生成后人工复核入库（数据集变更须重新审核——testkit.md §4.7）。
// =====================================================================

import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const inputPath = join(here, "..", "inputs", "robot.json");
const outputPath = join(here, "..", "expected", "namemap.json");

const model = JSON.parse(readFileSync(inputPath, "utf8"));
const device = model.robotLocalName;

const entries = [];

// robot → Device（设备名本身）＋ BaseMount（§6.3 安装帧——共享 robot 对象）。
entries.push({
  seed: model.objectSeeds.robot,
  scope: "Device",
  fullName: device,
  authoritativeLocalName: device,
});
entries.push({
  seed: model.objectSeeds.robot,
  scope: "BaseMount",
  fullName: device + ".BaseMount",
  authoritativeLocalName: "BaseMount",
});

// 逐关节 → Joint；逐连杆 → LinkFrame；首连杆另有 BaseFrame、末连杆另有
// Flange（§7.1 范围表）。
model.joints.forEach((joint, i) => {
  entries.push({
    seed: model.objectSeeds["j" + (i + 1)],
    scope: "Joint",
    fullName: device + "." + joint.localName,
    authoritativeLocalName: joint.localName,
  });
});
model.links.forEach((linkName, i) => {
  entries.push({
    seed: model.objectSeeds["l" + i],
    scope: "LinkFrame",
    fullName: device + "." + linkName,
    authoritativeLocalName: linkName,
  });
  if (i === 0) {
    entries.push({
      seed: model.objectSeeds["l" + i],
      scope: "BaseFrame",
      fullName: device + ".Base",
      authoritativeLocalName: "Base",
    });
  }
  if (i === model.links.length - 1) {
    entries.push({
      seed: model.objectSeeds["l" + i],
      scope: "Flange",
      fullName: device + ".Flange",
      authoritativeLocalName: "Flange",
    });
  }
});

// 存储序＝(scope, localName, ObjectId) 字典序（§7.2 稳定排序）——期望文件
// 按此排序，测试逐条对照后另以全量往返断言与映射自身对账。
// scopeOrder 与 NameScope 枚举声明序一致（NameMap.hpp：Device < Joint <
// LinkFrame < BaseMount < BaseFrame < Flange）。
const scopeOrder = {
  Device: 0, Joint: 1, LinkFrame: 2, BaseMount: 3, BaseFrame: 4, Flange: 5,
};
entries.sort(
  (a, b) =>
    scopeOrder[a.scope] - scopeOrder[b.scope] ||
    a.fullName.localeCompare(b.fullName),
);

const expected = {
  comment:
    "rt-namemap-roundtrip 期望面：§7.1 范围表在平面 2R 夹具上的全量条目" +
    "（无工具/场景/资源/Body——物性缺失路径）。测试断言：seed→ObjectId 后" +
    " resolveObjectId 全量命中本表（IRD_EXPECT_IDENTICAL 精确等值——附录 D 第 12 项），" +
    "且每条 fullName 经 resolveRuntimeName 反解回同一 ObjectId（AT-18 双向往返）。",
  entries,
};

writeFileSync(outputPath, JSON.stringify(expected, null, 2) + "\n", "utf8");
console.log("written:", outputPath, "entries:", entries.length);
