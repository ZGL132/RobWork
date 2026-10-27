# 随机/LHS 候选独立审查检索记录（REUSE-T36/WP-13，2026-10-25）
# 范围：kinematics SeedMix（splitmix64/streamSeedFor，64 位任务私有流）与 optimization SeededLhs/LHS；上游候选 rw::math::Random、rw::pathplanning::QSampler、RobWorkSim sandbox。零生产代码改动，维持矩阵 pending。

## S1 上游 Random 全表面（rw/math/Random.hpp + Random.cpp）
$ rg -n "static|seed|boost|ran|class" RobWork/RobWork/src/rw/math/Random.hpp RobWork/RobWork/src/rw/math/Random.cpp
```
RobWork/RobWork/src/rw/math/Random.cpp:23:#include <boost/random/mersenne_twister.hpp>
RobWork/RobWork/src/rw/math/Random.cpp:24:#include <boost/random/normal_distribution.hpp>
RobWork/RobWork/src/rw/math/Random.cpp:25:#include <boost/random/uniform_real.hpp>
RobWork/RobWork/src/rw/math/Random.cpp:26:#include <boost/random/variate_generator.hpp>
RobWork/RobWork/src/rw/math/Random.cpp:31:boost::mt19937 generator;
RobWork/RobWork/src/rw/math/Random.cpp:32:boost::uniform_real<> distributor;
RobWork/RobWork/src/rw/math/Random.cpp:34:typedef boost::normal_distribution< double > dist_type;
RobWork/RobWork/src/rw/math/Random.cpp:36:boost::variate_generator< boost::mt19937&, dist_type >
RobWork/RobWork/src/rw/math/Random.cpp:37:    normal_distribution (generator, boost::normal_distribution< double > (0, 1));
RobWork/RobWork/src/rw/math/Random.cpp:40:double Random::ranNormalDist (double mean, double sigma)
RobWork/RobWork/src/rw/math/Random.cpp:45:double Random::ran ()
RobWork/RobWork/src/rw/math/Random.cpp:50:void Random::seed (unsigned seed)
RobWork/RobWork/src/rw/math/Random.cpp:52:    // VC++ can't select the correct seed() method without a cast here.
RobWork/RobWork/src/rw/math/Random.cpp:53:    generator.seed (static_cast< boost::mt19937::result_type > (seed));
RobWork/RobWork/src/rw/math/Random.cpp:56:void Random::seed ()
RobWork/RobWork/src/rw/math/Random.cpp:58:    seed ((unsigned) rw::common::TimerUtil::currentTimeMs ());
RobWork/RobWork/src/rw/math/Random.cpp:61:double Random::ran (double from, double to)
RobWork/RobWork/src/rw/math/Random.cpp:72:        res = from + (to - from) * Random::ran ();
RobWork/RobWork/src/rw/math/Random.cpp:78:int Random::ranI (int from, int to)
RobWork/RobWork/src/rw/math/Random.cpp:80:    return (int) floor (Random::ran (from, to));
RobWork/RobWork/src/rw/math/Random.hpp:33:     * @brief Generation of random numbers.
RobWork/RobWork/src/rw/math/Random.hpp:35:    class Random
RobWork/RobWork/src/rw/math/Random.hpp:42:         * @brief A random double in the range [0, 1[ using a uniform distribution.
RobWork/RobWork/src/rw/math/Random.hpp:44:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:46:        static double ran ();
RobWork/RobWork/src/rw/math/Random.hpp:49:         * @brief Seeds the random number generator.
RobWork/RobWork/src/rw/math/Random.hpp:51:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:53:        static void seed (unsigned seed);
RobWork/RobWork/src/rw/math/Random.hpp:56:         * @brief Seeds the random number generator with current time of day
RobWork/RobWork/src/rw/math/Random.hpp:58:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:60:        static void seed ();
RobWork/RobWork/src/rw/math/Random.hpp:63:         * @brief A random double in the range [from, to[ using a uniform distribution.
RobWork/RobWork/src/rw/math/Random.hpp:65:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:67:        static double ran (double from, double to);
RobWork/RobWork/src/rw/math/Random.hpp:70:         * @brief A random integer in the range [from, to[ using a uniform distribution.
RobWork/RobWork/src/rw/math/Random.hpp:72:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:74:        static int ranI (int from, int to);
RobWork/RobWork/src/rw/math/Random.hpp:77:         * @brief Returns a random sample around \b mean with standard deviation \b sigma  using the
RobWork/RobWork/src/rw/math/Random.hpp:80:         * @note Uses boost::random
RobWork/RobWork/src/rw/math/Random.hpp:86:        static double ranNormalDist (double mean, double sigma);
```

## S2 上游 64 位随机/流候选反证（rw 全库）
$ rg -rn "uint64_t|uint64" RobWork/RobWork/src/rw/math/Random.hpp RobWork/RobWork/src/rw/pathplanning/ -g "*.hpp"
```
(无命中)
```

## S3 QSampler 种子/分层能力（rw/pathplanning）
$ rg -n "seed|ran|Random|shuffle|strat|layer" RobWork/RobWork/src/rw/pathplanning/QSampler.hpp RobWork/RobWork/src/rw/pathplanning/QSampler.cpp
```
RobWork/RobWork/src/rw/pathplanning/QSampler.cpp:104:    Q doSample () { return Math::ranQ (_bounds.first, _bounds.second); }
```

## S4 上游 LHS/拉丁超立方/分层采样候选反证（rw 全库＋RobWorkSim 全库）
$ rg -iln "latin|LHS|hypercube|HyperSphere" RobWork/RobWork/src/rw RobWork/RobWorkSim/src
```
RobWork/RobWorkSim/src\sandbox\grippers\setups\belt_box\DynamicMaterialDataBase.xml
RobWork/RobWorkSim/src\sandbox\grippers\setups\belt_pump\DynamicMaterialDataBase.xml
RobWork/RobWorkSim/src\sandbox\grippers\setups\belt_cyl\DynamicMaterialDataBase.xml
RobWork/RobWorkSim/src\sandbox\grippers\setups\belt_dolt\DynamicMaterialDataBase.xml
RobWork/RobWorkSim/src\rwsimlibs\swig\sdurwsim.i
RobWork/RobWorkSim/src\rwsim\simulator\PhysicsEngine.hpp
RobWork/RobWorkSim/src\rwsim\simulator\GraspTaskSimulator.hpp
RobWork/RobWorkSim/src\sandbox\grippers\GraspPlugin\csgjs.cpp
RobWork/RobWorkSim/src\rwsim\simulator\DynamicSimulator.hpp
RobWork/RobWorkSim/src\sandbox\grippers\GraspPlugin\TaskGenerator.hpp
RobWork/RobWorkSim/src\rwsim\control\SerialDeviceController.hpp
RobWork/RobWorkSim/src\sandbox\grippers\GraspPlugin\GripperTaskSimulator.cpp
RobWork/RobWork/src/rw\common\TimerUtil.hpp
RobWork/RobWorkSim/src\rwsim\sensor\TactileArraySensor.cpp
RobWork/RobWork/src/rw\common\Timer.hpp
RobWork/RobWork/src/rw\trajectory\TrajectoryFactory.hpp
RobWork/RobWorkSim/src\rwsim\util\RecursiveNewtonEuler.hpp
RobWork/RobWork/src/rw\trajectory\RampInterpolator.hpp
RobWork/RobWork/src/rw\trajectory\ParabolicBlend.hpp
RobWork/RobWorkSim/src\rwsimlibs\plugins\GSupportPosePlugin.hpp
RobWork/RobWorkSim/src\rwsimlibs\plugins\SimTaskVisPlugin.cpp
RobWork/RobWorkSim/src\rwsimlibs\plugins\GraspTableGeneratorPlugin.hpp
RobWork/RobWorkSim/src\rwsim\dynamics\DynamicUtil.hpp
RobWork/RobWorkSim/src\rwsim\dynamics\BodyUtil.hpp
RobWork/RobWorkSim/src\rwsimlibs\plugins\PSupportPosePlugin.hpp
RobWork/RobWorkSim/src\rwsimlibs\gui\GraspRestingPoseDialog.hpp
RobWork/RobWorkSim/src\rwsimlibs\gui\GraspRestingPoseDialog1.hpp
RobWork/RobWorkSim/src\rwsimlibs\gui\GraspSelectionDialog.hpp
RobWork/RobWorkSim/src\sandbox\grippers\GraspPlugin\sample\scripts\generate-landscape
RobWork/RobWorkSim/src\rwsimlibs\ode\ODESimulator.hpp
RobWork/RobWorkSim/src\rwsimlibs\gui\TactileSensorDialog.hpp
RobWork/RobWorkSim/src\rwsimlibs\gui\SupportPoseAnalyserDialog.hpp
RobWork/RobWork/src/rw\geometry\CMakeLists.txt
RobWork/RobWorkSim/src\rwsimlibs\gui\RestingPoseDialog.hpp
RobWork/RobWork/src/rw\geometry\Line.hpp
RobWork/RobWork/src/rw\geometry\IntersectUtil.hpp
RobWork/RobWork/src/rw\geometry\HyperSphere.hpp
RobWork/RobWork/src/rw\geometry\HyperSphere.cpp
RobWork/RobWork/src/rw\geometry\GeometryUtil.hpp
RobWork/RobWork/src/rw\loaders\xml\XercesUtils.cpp
RobWork/RobWork/src/rw\geometry\Plane.hpp
RobWork/RobWork/src/rw\proximity\ProximityCalculator.hpp
RobWork/RobWork/src/rw\proximity\rwstrategy\BVTreeFactory.hpp
RobWork/RobWork/src/rw\proximity\DistanceCalculator.hpp
RobWork/RobWork/src/rw\invkin\ClosedFormIK.hpp
RobWork/RobWork/src/rw\proximity\CMakeLists.txt
RobWork/RobWork/src/rw\proximity\ProximityStrategy.hpp
RobWork/RobWork/src/rw\invkin\PieperSolver.hpp
RobWork/RobWork/src/rw\geometry\Triangulate.hpp
RobWork/RobWork/src/rw\invkin\JacobianIKSolverM.hpp
RobWork/RobWork/src/rw\invkin\JacobianIKSolver.hpp
RobWork/RobWork/src/rw\invkin\IterativeMultiIK.hpp
RobWork/RobWork/src/rw\invkin\IterativeIK.hpp
RobWork/RobWork/src/rw\models\DeformableObject.hpp
RobWork/RobWork/src/rw\invkin\InvKinSolver.hpp
RobWork/RobWork/src/rw\models\Device.hpp
RobWork/RobWork/src/rw\graspplanning\ApproachMeasure3D.hpp
RobWork/RobWork/src/rw\math\EAA.hpp
RobWork/RobWork/src/rw\pathplanning\PathAnalyzer.hpp
RobWork/RobWork/src/rw\math\Quaternion.cpp
RobWork/RobWork/src/rw\math\Quaternion.hpp
RobWork/RobWork/src/rw\math\Rotation3D.hpp
RobWork/RobWork/src/rw\math\Statistics.hpp
RobWork/RobWork/src/rw\math\Transform3DVector.hpp
RobWork/RobWork/src/rw\math\Vector3D.hpp
RobWork/RobWork/src/rw\math\VectorND.hpp
```

## S5 RobWorkSim sandbox 构建注册（默认关闭证据）
$ rg -n "sandbox" RobWork/RobWorkSim/src/CMakeLists.txt
```
(无命中)
```
NewtonEulerDynamics.cpp
NewtonEulerDynamics.hpp
grippers

## S4r 修正检索（S4 的 -i 裸 LHS 命中全为 lhs=left-hand-side 形参名假阳性——作废，换词边界/大小写敏感）
$ rg -ln "LHS|Latin|Hypercube|hypercube|stratified" RobWork/RobWork/src/rw RobWork/RobWorkSim/src -g "*.hpp" -g "*.cpp"
```
RobWork/RobWork/src/rw\loaders\xml\XercesUtils.cpp
```

## S6 上游随机采样面补充：QNormalDistribution/QRandomSampler 等邻近入口
$ ls RobWork/RobWork/src/rw/pathplanning/ | grep -i "sampler" 与 rg -l "class.*Sampler" RobWork/RobWork/src/rw/math
```
QIKSampler.cpp
QIKSampler.hpp
QSampler.cpp
QSampler.hpp
QToQSamplerPlanner.cpp
QToQSamplerPlanner.hpp
```

## S6b 结论补记
- rw/math 无 Sampler 类（rg -ln "Sampler" RobWork/RobWork/src/rw/math/ 零命中）
- XercesUtils.cpp:164 命中为 Xerces 字符常量 chLatin_L（拉丁字母），非拉丁超立方——S4r 实际零有效命中

## S7 产品随机消费点现状（只读登记，不改动）
$ rg -n "streamSeedFor|splitmix64" <kinematics/src> <optimization/src>
```
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:4: *         校验与 canonical 序列化（"IRDOPTC1"）、确定性候选生成（splitmix64
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:13: * ——validateConfiguration）；②随机性唯一来自自持 splitmix64（黄金常数
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:95:// 自持 splitmix64 PRNG（确定性唯一随机源——I-OPT-3/NFR-COR-02）
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:99: * @brief splitmix64 自持伪随机数发生器状态。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:103: * "同种子 ⇒ 同候选集合"跨平台成立（WP-23-T09 复现基准的前置）。splitmix64
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/EvaluatorPorts.cpp:445:    // 第 4 步：自持 splitmix64（状态＝种子——同种子同输出，跨平台确定）。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:472:// ★ 审查 G12 收拢（WP-15-T20 第三批）：splitmix64/streamSeedFor 算法与
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:475:using kseed::splitmix64;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:476:using kseed::streamSeedFor;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:518:        // （streamSeedFor——KIN-P5/审查 A06：旧"seed·2³²＋k"在模 2⁶⁴
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:520:        // 逐轴 u_k,i∈[0,1)（splitmix64 按轴序连续派生——同 seed 同矩阵）。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:523:            std::uint64_t state = streamSeedFor(seed, k);  // 初值序入混合
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:530:                q.push_back(lo + unitFromU64(splitmix64(state)) * (hi - lo));
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:21: * 确定性：探针种子＝(query.seed, probeSerial) 的 streamSeedFor 完整
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:56:using kseed::splitmix64;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:57:using kseed::streamSeedFor;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:60:/// (seed, probeSerial) 经 streamSeedFor 完整 64 位混合（A06——probeSerial
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Envelope.cpp:65:    return streamSeedFor(seed, probeSerial);
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointStrategy.cpp:151: * @brief 自持 splitmix64（确定性唯一随机源——与 EvaluatorPorts.cpp 同款
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointStrategy.cpp:819:    // ---- 第 4 步：自持 splitmix64（状态＝种子——同 T06 纪律：状态初值
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Generator.cpp:11: * 确定性：seeded-lhs 路径随机性唯一来自 splitmix64（被委托函数内自持）；
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Generator.cpp:55:    // 候选批逐字节委托 WP-20-T06 生成函数（单一实现点——splitmix64/层置换/
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:17: *   - Grid 体心规则/Random splitmix64 序列/斐波那契螺旋/roll 体心均分
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:123:using kseed::splitmix64;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:124:using kseed::streamSeedFor;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:314:    // 64 位混合（streamSeedFor——KIN-P5/审查 A06：旧"seed·2³²＋k"丢弃
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:319:        std::uint64_t state = streamSeedFor(seed, k);
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:320:        const double u0 = unitFromU64(splitmix64(state));
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:321:        const double u1 = unitFromU64(splitmix64(state));
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Sampling.cpp:322:        const double u2 = unitFromU64(splitmix64(state));
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:156:// ★ 审查 G12 收拢（WP-15-T20 第三批）：splitmix64/streamSeedFor 算法与
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:159:constexpr auto splitmix64Local = kseed::splitmix64;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:160:constexpr auto streamSeedForLocal = kseed::streamSeedFor;
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:193: *     混合（streamSeedForLocal——A06 同源：旧式 "seed·2³²＋k" 丢弃
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:194: *     seed 高 32 位），逐轴 u∈[0,1) 由 splitmix64 连续派生（轴 0→n−1），
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:226:            std::uint64_t state = streamSeedForLocal(seed, k);
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Workspace.cpp:233:                q.push_back(lo + unitFromU64Local(splitmix64Local(state)) * (hi - lo));
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:3: * @brief  kinematics 单元内共享的确定性种子派生（splitmix64 流种子混合
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:9: *   - KIN-P5/审查 A06 修复（splitmix64 双射 avalanche——异种子必异序列
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:33:/// splitmix64（确定性伪随机序列源；算法公开定版、跨平台位级一致）。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:34:inline std::uint64_t splitmix64(std::uint64_t& state)
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:53:///   a ＝ splitmix64(seed)——seed 全 64 位 avalanche（高位翻转雪崩到
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:55:///   b ＝ splitmix64(index ⊕ 黄金常量)——序号独立 avalanche（⊕常量
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:57:///   输出 ＝ splitmix64(a ⊕ b)——再 avalanche 消除异或的线性结构。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:60:/// splitmix64 双射 avalanche 实际不可构造）。
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:61:inline std::uint64_t streamSeedFor(std::uint64_t seed, std::uint64_t index)
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:64:    const std::uint64_t a = splitmix64(s);
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:66:    const std::uint64_t b = splitmix64(t);
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/SeedMix.hpp:68:    return splitmix64(u);
```

## S8 结论：sandbox 无优化/LHS 目标
- RobWorkSim 根与 src CMakeLists 均无 add_subdirectory(sandbox)——目标未登记默认不建；src/sandbox/ 实际内容仅 NewtonEulerDynamics 与 grippers（ls 实证）
- required-proposals.md 登记在案：上游sandbox优化目标默认关闭，当前API/安装适用性待实测；数学Random和HyperSphere不能直接承诺旧样本身份与顺序
