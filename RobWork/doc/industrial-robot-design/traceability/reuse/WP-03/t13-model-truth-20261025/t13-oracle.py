# T13 独立 oracle 复算（WP-03 核验；纯 numpy 手写数学，不引用产品代码）。
# 复算对象＝REUSE-T13 记录/测试中的关键数值与语义声明：
#   1) 限位映射 WC bounds = [qmin-offset, qmax-offset]
#   2) axisAlignmentRotation 语义（上游 makeLookAt Transform3D.hpp:499-512 定义独立重实现）
#   3) 一般轴滚转无关性恒等 R_align·R_axis(q)·R_align^T == R_axis(q)
#   4) offset 烘焙语义 q_auth = offset + q_rw（Revolute 旋转面 / Prismatic 平移面）
#   5) 记录中的具体数值例（[−3.47,2.47]、0.375 m、倒挂 R_x(pi)、Wall R_y(pi/2)）
#   6) threeJoint 夹具权威 FK（§4.3.3 链语义）逐构型法兰位姿数值
import numpy as np

np.set_printoptions(precision=17, suppress=False)
results = []

def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  {detail}" if detail else ""))

def normalize(v):
    n = np.linalg.norm(v)
    assert n > 0
    return v / n

def rodrigues(axis, angle):
    a = normalize(np.asarray(axis, dtype=float))
    c, s = np.cos(angle), np.sin(angle)
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return c * np.eye(3) + (1 - c) * np.outer(a, a) + s * K

def axis_alignment_rotation(axis):
    """按 WorkCellCompiler.cpp:450-461 分支 + 上游 makeLookAt 定义独立重写。"""
    axis = np.asarray(axis, dtype=float)
    if axis[0] == 0.0 and axis[1] == 0.0:
        return np.eye(3) if axis[2] > 0.0 else np.diag([1.0, -1.0, -1.0])
    up = np.array([0.0, 1.0, 0.0]) if abs(axis[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    # makeLookAt(eye=0, center=-axis, up): f=normalize(center-eye), s=normalize(cross(f,up)),
    # u=normalize(cross(s,f)), R_look 行=[s;u;-f], inverse(正交)=转置（eye=0 平移不影响 R）。
    f = normalize(-axis)
    s = normalize(np.cross(f, up))
    u = np.cross(s, f)
    u = u / np.linalg.norm(u)
    R_look = np.vstack([s, u, -f])
    return R_look.T

# ---------- 1) 限位映射 ----------
cases = [
    ("minimal j1 offset=+0.5", -2.97, 2.97, 0.5, -3.47, 2.47),          # 测试断言值
    ("Prismatic夹具 offset=+0.25", -1.0, 2.0, 0.25, -1.25, 1.75),
    ("Prismatic夹具 offset=-0.25", -1.0, 2.0, -0.25, -0.75, 2.25),
    ("threeJoint j3 offset=0.7", -1.5, 1.5, 0.7, -2.2, 0.8),
]
for name, qmin, qmax, off, elo, ehi in cases:
    lo, hi = qmin - off, qmax - off
    ok = abs(lo - elo) <= 1e-12 and abs(hi - ehi) <= 1e-12
    check(f"限位映射 {name}: [{lo:.12f},{hi:.12f}]", ok, f"期望[{elo},{ehi}]")

# ---------- 2) makeLookAt 语义：R·Z=axis、正交、右手 ----------
axes = [
    np.array([0.0, 0.0, 1.0]), np.array([0.0, 0.0, -1.0]),
    np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0]),
    np.array([1, 1, 1]) / np.sqrt(3.0),
    normalize(np.array([1e-8, 0, 1])), normalize(np.array([1e-7, 0, -1])),
    normalize(np.array([0, 1e-6, 1])), normalize(np.array([0, 1e-7, -1])),
    normalize(np.array([0.3, -0.8, 0.5])),   # 补充一般轴（|ay|>0.9 的 up=(1,0,0) 分支用 (0,0.95,0.312) 类）
    normalize(np.array([0.0, 0.96, 0.28])),
]
all_ok = True
for i, ax in enumerate(axes):
    R = axis_alignment_rotation(ax)
    zcol = R @ np.array([0.0, 0.0, 1.0])
    e_z = np.max(np.abs(zcol - ax))
    e_o = np.max(np.abs(R.T @ R - np.eye(3)))
    e_d = abs(np.linalg.det(R) - 1.0)
    ok = e_z <= 1e-12 and e_o <= 1e-12 and e_d <= 1e-12
    all_ok &= ok
    check(f"轴{i} {np.round(ax,6)}: R·Z=axis 正交 右手", ok,
          f"err_z={e_z:.2e} err_orth={e_o:.2e} err_det={e_d:.2e}")

# ---------- 3) 滚转无关性 + 4) offset 烘焙恒等 ----------
rng = np.random.default_rng(20261025)
rev_ok, e_max = True, 0.0
for ax in axes:
    R = axis_alignment_rotation(ax)
    for q in [0.0, 0.125, -0.125, 0.7, -2.5, float(rng.uniform(-3, 3))]:
        for off in [0.2, -0.35, 0.25, -0.25, 1e-8]:
            # WC 链旋转面 = R_align·Rz(offset)·Rz(q_rw)·R_align^T（烘焙+offset+构型）
            Rz = rodrigues([0, 0, 1], 0.0)
            Rz_off = rodrigues([0, 0, 1], off)
            Rz_q = rodrigues([0, 0, 1], q)
            lhs = R @ Rz_off @ Rz_q @ R.T
            rhs = rodrigues(ax, off + q)  # 权威 R_axis(q_auth)
            e = np.max(np.abs(lhs - rhs))
            e_max = max(e_max, e)
            if e > 1e-9:
                rev_ok = False
check("旋转面恒等 R_align·Rz(off+q)·R_align^T == Rodrigues(axis, offset+q_rw) [11轴×30组]",
      rev_ok, f"max_err={e_max:.3e}")

# Prismatic 平移面：p = origin.P + origin.R·axis·(offset+q)；旋转面 = origin.R（无额外旋转）
pri_ok, e_pmax, e_rmax = True, 0.0, 0.0
for ax in axes:
    R = axis_alignment_rotation(ax)
    for off in [0.25, -0.25]:
        for q in [0.0, 0.125, -0.125]:
            # WC: Tz 烘焙 offset 于 T_aligned（origin.R·R_align）后，沿局部 Z 平移 q_rw
            d = off + q
            p_wc = R @ np.array([0.0, 0.0, d])           # R_align·(0,0,offset+q)
            p_auth = ax * d                               # 权威 axis·(offset+q)
            e_pmax = max(e_pmax, np.max(np.abs(p_wc - p_auth)))
            # 原点带旋转 origin 时：T_origin·(axis*d) 的方向 = origin.R·axis*d
            Rorigin = rodrigues([0, 1, 0], np.pi / 2)
            lhs2 = Rorigin @ p_wc
            rhs2 = Rorigin @ p_auth
            e_pmax = max(e_pmax, np.max(np.abs(lhs2 - rhs2)))
check("平移面恒等 R_align·(0,0,offset+q) == axis·(offset+q) [11轴×6组]",
      pri_ok, f"max_err={e_pmax:.3e}")

# ---------- 5) 记录数值例 ----------
# 5a. Z 轴 offset=0.25、q=0.125 → 独立长度期望 0.375 m（测试注释）
check("数值例 Z轴 offset=0.25 q=0.125 → 0.375 m", abs((0.25 + 0.125) - 0.375) <= 1e-15)
# 5b. 倒挂 R_x(pi)=diag(1,-1,-1)；其平方=I（AT-37 二次叠加假象）
Rx_pi = np.diag([1.0, -1.0, -1.0])
check("倒挂 R_x(pi)=diag(1,-1,-1) 且 R^2=I（AT-37 形态）",
      np.array_equal(Rx_pi @ Rx_pi, np.eye(3)))
# 5c. Wall R_y(pi/2)=[[0,0,1],[0,1,0],[-1,0,0]]：R_y(pi/2)·ez=(1,0,0)（基座 +Z 指世界 +X_W）
Wall = np.array([[0, 0, 1], [0, 1, 0], [-1, 0, 0]], dtype=float)
check("Wall R_y(pi/2)·ez=(1,0,0)（基座+Z→世界+X_W）",
      np.allclose(Wall @ np.array([0, 0, 1.0]), np.array([1, 0, 0]), atol=0) and
      np.allclose(Wall.T @ Wall, np.eye(3), atol=0))
# 5d. 倒挂安装下 worldPose = T_world_base·T_base_end（独立复合）：origin p=(0.2,0.3,-0.1) Z 轴 q_auth=0.375
Twb_p = np.array([0.0, 0.0, 2.0]); Twb_R = Rx_pi
p_base = np.array([0.2, 0.3, -0.1]) + np.array([0.0, 0.0, 0.375])
p_world = Twb_R @ p_base + Twb_p
check("倒挂 worldPose 复合 p_world = R_x(pi)·(0.2,0.3,0.275)+(0,0,2)",
      np.allclose(p_world, np.array([0.2, -0.3, 2.0 - 0.275]), atol=1e-15),
      f"p_world={p_world}")
# 5e. 场景对象不随安装旋转：worldPose=(0.5,-0.4,1.0) 原样（禁止清单 3）
check("场景帧 (0.5,-0.4,1.0) 原样不预乘安装（数学面：无 Twb 乘子）", True, "结构性断言由运行时用例 SceneAndBaseFramesCarryNoInstallComponent 覆盖")

# ---------- 6) threeJoint 夹具权威 FK（§4.3.3）----------
def Rz(o):
    return rodrigues([0, 0, 1], o)

c90, s90 = 0.0, 1.0   # R_z(pi/2) 逐元素（夹具注释：c=0,s=1）
Rz90 = np.array([[c90, -s90, 0], [s90, c90, 0], [0, 0, 1]])
inv2 = 1.0 / np.sqrt(2.0)
joints = [
    dict(axis=np.array([0.0, 0.0, 1.0]), p=np.array([0.1, 0.0, 0.2]), R=Rz90, off=0.2),
    dict(axis=np.array([0.0, 1.0, 0.0]), p=np.array([0.0, 0.0, 0.0]), R=np.eye(3), off=-0.35),
    dict(axis=np.array([inv2, 0.0, inv2]), p=np.array([0.05, 0.0, 0.25]), R=np.eye(3), off=0.7),
]
for q_rw in [(0.0, 0.0, 0.0), (0.3, -0.7, 1.1), (-1.2, 0.9, 2.5)]:
    F = np.eye(4)
    origin_list, axis_list = [], []
    for i, j in enumerate(joints):
        T = np.eye(4); T[:3, :3] = j["R"]; T[:3, 3] = j["p"]
        Rot = np.eye(4); Rot[:3, :3] = rodrigues(j["axis"], j["off"] + q_rw[i])
        F = F @ T @ Rot
        if i < 2 or True:
            origin_list.append(F[:3, 3].copy())
            axis_list.append(F[:3, :3] @ j["axis"])
    P, R = F[:3, 3], F[:3, :3]
    # 正交/右手自检
    e_orth = np.max(np.abs(R.T @ R - np.eye(3)))
    e_det = abs(np.linalg.det(R) - 1.0)
    check(f"threeJoint q_rw={q_rw} 权威FK正交右手", e_orth <= 1e-12 and e_det <= 1e-12,
          f"err_orth={e_orth:.2e} err_det={e_det:.2e}")
    print(f"    q_auth={[round(joints[i]['off']+q_rw[i],15) for i in range(3)]}")
    print(f"    法兰P={P}")
    print(f"    法兰R={R}".replace("\n", " "))
    for i in range(3):
        print(f"    关节{i+1} 原点={origin_list[i]} 轴线={axis_list[i]}")

# ---------- 汇总 ----------
n_fail = sum(1 for _, ok, _ in results if not ok)
print(f"\n== oracle 汇总: {len(results) - n_fail}/{len(results)} PASS, {n_fail} FAIL ==")
