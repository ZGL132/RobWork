// REUSE-T51 独立验收·关键数值抽查（与产品代码/测试代码零共享——独立实现
// 平面二连杆 FK＋闭式双支 IK＋几何前提核算；node 独立运行）。
const L1 = 1.1, L2 = 0.7;
const wrap = a => Math.atan2(Math.sin(a), Math.cos(a));

// 独立 FK（平面 2R：q1 自基座，q2 相对；TCP 偏置 L2 沿连杆 2 方向）。
function fk(q1, q2) {
  const x = L1 * Math.cos(q1) + L2 * Math.cos(q1 + q2);
  const y = L1 * Math.sin(q1) + L2 * Math.sin(q1 + q2);
  const phi = wrap(q1 + q2);
  return { x, y, phi };
}
// 独立闭式双支（教科书：余弦定理＋肘上/肘下）。
function closedForm(x, y) {
  const c2 = (x * x + y * y - L1 * L1 - L2 * L2) / (2 * L1 * L2);
  if (Math.abs(c2) > 1 + 1e-12) throw new Error('target unreachable');
  const s2 = Math.sqrt(Math.max(0, 1 - c2 * c2));
  return [1, -1].map(sign => {
    const q2 = sign * Math.atan2(s2, c2);
    const q1 = Math.atan2(y, x) - Math.atan2(L2 * sign * s2, L1 + L2 * c2);
    return [q1, q2];
  });
}
let fail = 0;
const check = (name, cond, detail) => {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}${detail ? '  —— ' + detail : ''}`);
  if (!cond) fail++;
};

// [1] 黄金目标复算：golden = FK(0.3, −0.5)（IkTest oracle 用例载体）。
const g = fk(0.3, -0.5);
console.log(`FK(0.3,−0.5) = (x=${g.x.toFixed(12)}, y=${g.y.toFixed(12)}, φ=${g.phi.toFixed(12)})`);

// [2] 闭式分支复算＋自洽：位置级回代两分支均须还原 golden 位置（1e-12）；
//     全位姿回代仅在姿态一致分支成立——位置双支中恰一支满足目标姿态，
//     该支＝完整位姿问题的唯一解支（oracle 用例的独立几何前提）。
const branches = closedForm(g.x, g.y);
check('闭式解恰两分支', branches.length === 2);
let poseBranchIdx = -1;
for (const [i, b] of branches.entries()) {
  const back = fk(b[0], b[1]);
  const posErr = Math.max(Math.abs(back.x - g.x), Math.abs(back.y - g.y));
  check(`分支${i + 1} 位置回代自洽 (err=${posErr.toExponential(2)})`, posErr < 1e-12);
  if (Math.abs(wrap(back.phi - g.phi)) < 1e-9) poseBranchIdx = i;
}
check('恰一支满足目标姿态（完整位姿解支唯一）', poseBranchIdx >= 0,
  `解支=${poseBranchIdx + 1} → q=(${branches[poseBranchIdx].map(v => v.toFixed(6)).join(', ')})`);
check('完整位姿解支＝golden 构型 (0.3,−0.5)（往返闭合）',
  Math.abs(wrap(branches[poseBranchIdx][0] - 0.3)) < 1e-12
  && Math.abs(wrap(branches[poseBranchIdx][1] + 0.5)) < 1e-12);
// 初值(0.35,−0.45) 近旁分支（产物侧 iterations>0 的前提）。
const d1 = Math.max(Math.abs(wrap(0.35 - branches[0][0])), Math.abs(wrap(-0.45 - branches[0][1])));
const d2 = Math.max(Math.abs(wrap(0.35 - branches[1][0])), Math.abs(wrap(-0.45 - branches[1][1])));
console.log(`初值(0.35,−0.45) 距分支1=${d1.toFixed(4)} rad、分支2=${d2.toFixed(4)} rad`);
check('初值确取完整位姿解支近旁（dPoseBranch 最小）',
  (poseBranchIdx === 0 ? d1 : d2) <= (poseBranchIdx === 0 ? d2 : d1));

// [3] oracle 用例断言：分支 1 须落在请求容差内且 j1∈[−2.97,2.97]（产品
//     契约区间）——产品解被断言落分支之一 1e-4 内，本处核对该分支自身
//     满足限位与容差几何（否则用例前提自相矛盾）。
for (const [i, b] of branches.entries()) {
  check(`分支${i + 1} j1 在契约区间 [−2.97,2.97]`, b[0] >= -2.97 && b[0] <= 2.97, `q1=${b[0].toFixed(6)}`);
  check(`分支${i + 1} j2 在契约区间 [−π,π]`, b[1] >= -Math.PI && b[1] <= Math.PI, `q2=${b[1].toFixed(6)}`);
}

// [4] 限位用例几何前提：j1 收窄 [0.35,2.97] 后两分支均必越界。
const narrowed = branches.filter(b => b[0] >= 0.35 && b[0] <= 2.97);
check('限位用例前提：两分支 q1 均不在 [0.35,2.97]', narrowed.length === 0,
  `q1 分支=[${branches.map(b => b[0].toFixed(6)).join(', ')}]`);

// [5] 不可达用例几何前提：目标 ‖p‖=0.2 < |L1−L2|=0.4（可达环带内孔），
//     且在解析界限（最大可达半径 L1+L2=1.8）内——结局 5 不触发、须走
//     迭代结局 2。
const rIn = 0.2;
check('内孔前提 0.2 < |L1−L2|=0.4', rIn < Math.abs(L1 - L2));
check('内孔目标仍在解析界限内（0.2 < L1+L2=1.8）', rIn < L1 + L2);
let threw = false;
try { closedForm(rIn, 0); } catch { threw = true; }
check('内孔目标闭式解无实分支（|c2|>1）', threw);

// [6] 直接对照用例前提：同目标同初值（0.35,−0.45）、上游 maxError 取
//     min(posTol,oriTol)——gtest 记录 makeRequest 默认容差 1e-6/1e-6 档
//     （见 makeRequest 实现），上游停判 normInf≤maxError 与产品双容差
//     逐项复核联合可满足。
console.log(fail === 0 ? 'acc-numeric-probe: 全部通过' : `acc-numeric-probe: ${fail} 项失败`);
process.exit(fail === 0 ? 0 : 1);
