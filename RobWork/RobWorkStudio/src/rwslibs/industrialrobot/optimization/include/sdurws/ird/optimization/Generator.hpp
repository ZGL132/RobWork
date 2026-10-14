/**
 * @file   Generator.hpp
 * @brief  搜索策略扩展点接口（ICandidateGenerator——OPT-10）与首版两个内置
 *         策略实现（seeded-lhs 适配器＋baseline-perturb 邻域扰动）。
 *
 * 设计依据：
 *   - units/optimization.md §8.2（搜索策略接口扩展点——`ICandidateGenerator`
 *     按策略生成候选批；首版可用策略词表 opt.strategy.seeded-lhs／
 *     opt.strategy.baseline-perturb；首版**不实现**进化/梯度等先进算法
 *     〔OPT-10"首版不要求一次性引入"〕；**不预建无语义的空算法占位**；
 *     扩展点＝注册新 ICandidateGenerator 实现〔strategyId 登记＋config.opt
 *     引用〕；基线候选恒生成；生成器受 OptimizationBudget 约束）、§8.5
 *     （鲁棒性/灵敏度反馈仅作用于搜索聚焦＝生成器提示——反馈驱动的剪枝/
 *     聚焦属于本扩展点的消费语义，不新增接口族）、§12.1（ICandidateGenerator
 *     ——O6/OPT-10 搜索策略接口）、§12.2（接口签名 Draft：strategyId()＋
 *     generateNextBatch(GenerationContext)；批间取消＝返回空批＋cancelled
 *     标记；生成纯函数式推进，无隐藏状态依赖线程时序）、§12.3（确定性——
 *     随机性仅来自显式种子 config.opt.seed）
 *   - REQUIREMENTS 附录 C《P-04 冻结数值表》表①（扰动幅度档位：默认
 *     ±1%×该变量行程〔归一化 0.01〕、工程策略可放宽上界 ±4%＝4× 默认封顶；
 *     依据列明文"幅度归工程策略配置面，冻结值即其默认"——本策略的扰动比例
 *     默认值/上界即引自该冻结采样面值；**判定阈值两槽位〔通过线/降级线〕
 *     维持待定，与本策略无关——本文件不涉任何判定面数值**）
 *   - 需求 OPT-10（搜索策略接口扩展——D/R2）、NFR-COR-02（同输入同种子
 *     ⇒ 等价候选集合）、NFR-MNT-04（不预建空占位）、OPT-02（改型项目默认
 *     锁定非授权参数——扰动集＝授权集）
 *   - 任务契约 tasks/foundation/WP-21-T06.json（acceptance：接口扩展评审
 *     通过；不预建空算法占位）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§14.2 行"WP-21-T06 实现搜索策略接口
 *   扩展 | §8.2（ICandidateGenerator 扩展点）| 接口扩展评审通过；禁止预建
 *   空算法占位"——本任务交付：①扩展点接口（GenerationContext/GenerationBatch/
 *   ICandidateGenerator——§12.2 Draft 的 C++17 具体化）；②§8.2 表两行的
 *   **已实现**策略：seeded-lhs（适配既有 generateSeededLhsCandidates 单点——
 *   WP-20-T06 交付面零漂移）与 baseline-perturb（本批新实现——WP-20-T06
 *   落位登记预告的"§8.2 表第二行实现随 WP-21-T06"）；③内置词表聚合与
 *   分派单点（builtInStrategyIds/generateWithBuiltInStrategy——
 *   validateConfiguration 检查⑨与编排器 run() 同源消费）；④编排器生成步
 *   骤按策略分派（deps.generator 注入优先，空则内置分派——词表扩展后
 *   config.strategyId=baseline-perturb 的配置可真实运行，不接受而按旧策略
 *   执行＝静默错配，NFR-COR-03 同源纪律）。
 *   **不落**（如实登记）：多代搜索编排循环（budget.maxGenerations 维持 ==1
 *   校验——"第 1 代之后是什么"的搜索语义〔反馈聚焦/剪枝〕归 §8.5 分层联合
 *   策略 WP-21-T03，本批不接受 maxGenerations>1 而只跑 1 代的静默降级）；
 *   离散器件组合空间采样（§5.6 R2——归 WP-21-T03 外层探索消费面）；进化/
 *   梯度等先进算法（OPT-10 明文首版不引入）；focusHints 的生产面（§8.6
 *   模式③敏感度排序反馈随 WP-21-T05——本批只落接口消费位，内置策略不消费，
 *   不伪造聚焦语义）。
 *
 * 背景说明（第一读者须知——三件事）：
 *   ① **策略即身份要素**：config.strategyId 进 config.opt canonical（§4.5
 *      失效映射"策略进 config.opt"）——切换策略＝新 sliceId＝缓存不命中；
 *      本文件不新增 canonical 字段（baseline-perturb 的扰动比例走生成器
 *      配置面 BaselinePerturbParams，见下注），§4.3 契约面零变化。
 *   ② **词表只登记已实现策略**（NFR-MNT-04）：builtInStrategyIds() 返回的
 *      每个 token 都有真实实现类与生成算法（契约测试逐 token 解析钉住）；
 *      未实现的策略名不得进词表——"登记而不可解析"即空占位违约。
 *   ③ **确定性两态**：seeded-lhs 的随机性唯一来自自持 splitmix64（config.
 *      seed——WP-20-T06 单点复用）；baseline-perturb 的邻域枚举**无随机
 *      自由度**（±Δ 双向轮转调度——坐标搜索式确定性枚举，比种子更强）：
 *      同 config 双跑必同批；种子变化改变身份（重算）但不改变本策略批
 *      ——策略语义即确定性邻域枚举，无随机项可播种。
 *
 * 线程约束：全部纯函数/无状态（BaselinePerturbGenerator 成员为构造期定型的
 *   只读参数）——可重入；编排器侧消费线程约束见 EvaluatorPorts.hpp（run()
 *   串行）。
 * 确定性：同 (ctx, 构造参数) ⇒ 同 GenerationBatch（逐字节同补丁序列——补丁
 *   经 makeCandidatePatch canonical 唯一编码层构造）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_GENERATOR_HPP
#define SDURWS_IRD_OPTIMIZATION_GENERATOR_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidatePatch/PatchItem/
                                                        //  makeCandidatePatch——补丁唯一
                                                        //  编码构造层（§5.5/§8.2）
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // OptimizationConfiguration/
                                                        //  kStrategySeededLhs——config.opt
                                                        //  载荷与既有策略 token

namespace sdurws::ird::optimization {

// =====================================================================
// 搜索策略词表（§8.2 表——只登记已实现策略，NFR-MNT-04）
// =====================================================================

/**
 * @brief 改型邻域扰动策略 token（卡 §8.2 表第二行 "opt.strategy.baseline-
 *        perturb"——基线邻域扰动：改型场景下对锁定集外授权变量做小步扰动，
 *        步长/比例入配置；B/D 两阶段）。
 *
 * 进 config.opt canonical（策略 token 变化 ⇒ sliceId 变 ⇒ 缓存失效，§4.5）。
 * 词表边界：本 token 与 kStrategySeededLhs（EvaluatorPorts.hpp——T06-of-WP-20
 * 落位点，token 常量保持原址零迁移）构成首批已实现词表，聚合面见
 * builtInStrategyIds()；未实现策略名不得登记（§8.2"不预建无语义的空算法
 * 占位"——NFR-MNT-04）。
 */
inline constexpr std::string_view kStrategyBaselinePerturb
    = "opt.strategy.baseline-perturb";

/**
 * @brief 邻域扰动比例默认值 0.01（±1%×变量行程）。
 *
 * 来源＝REQUIREMENTS 附录 C《P-04 冻结数值表》表①冻结值（2026-10-13 所有者
 * 确认冻结；其依据列对卡 §8.2 baseline-perturb 行明文注"幅度归工程策略配置
 * 面，冻结值即其默认"——即本常量的权威出处）。语义：连续变量的单步扰动
 * 幅度 Δ＝perturbRatio×(upperBound−lowerBound)（按该变量行程归一化，同表①
 * "±1%×该变量行程"口径）。这是**采样面**冻结值——判定阈值两槽位（通过线/
 * 降级线）维持待定且与本策略无关（本策略不产出任何判定结论）。
 */
inline constexpr double kBaselinePerturbDefaultRatio = 0.01;

/**
 * @brief 邻域扰动比例放宽上界 0.04（4× 默认封顶）。
 *
 * 来源＝附录 C 表①"工程策略可放宽上界 ±4%（4×默认封顶）"——同表①冻结
 * 结构（与《P-06 冻结数值表》4× 封顶先例同型）。BaselinePerturbParams 校验
 * 拒绝 perturbRatio>0.04（超出冻结放宽档位的配置不静默接受——NFR-COR-03）。
 */
inline constexpr double kBaselinePerturbMaxRatio = 0.04;

// =====================================================================
// 生成上下文与批（§12.2 接口签名的配套数据面——Draft 具体化）
// =====================================================================

/**
 * @brief 单次候选批生成的冻结输入面（§12.2 generateNextBatch 入参）。
 *
 * 生命周期：调用方持有并保证 generateNextBatch() 调用期间存活（生成器内部
 * 值语义拷贝）。值语义；线程安全：纯值。
 */
struct GenerationContext {
    /// 已校验运行配置（变量/种子/预算/策略/目标——采样域与预算单一来源；
    /// 生成器入口防御复验，同 generateSeededLhsCandidates 口径）。
    OptimizationConfiguration config {};
    /// 生成代序号（0 起）。R1/首批内置策略均为单批策略：仅 generationIndex
    /// ==0 产出候选批，>0 返回空批＋exhausted（多代推进的消费编排归 §8.5
    /// 分层联合策略 WP-21-T03——本批不接受 maxGenerations>1 的静默降级，
    /// validateConfiguration 检查⑦维持 ==1）。
    std::uint32_t generationIndex = 0;
    /// 搜索聚焦提示（§8.5"鲁棒性反馈仅作用于搜索聚焦（生成器提示）"的接口
    /// 消费位——稳定 token 串，生产面＝§8.6 模式③敏感度排序反馈，随
    /// WP-21-T05 落位）。**首批内置策略不消费本位**（如实登记：消费语义随
    /// 反馈生产面同批落位，不预建聚焦算法）；外层联合策略（WP-21-T03）经
    /// 自定义 ICandidateGenerator 实现消费。
    std::vector<std::string> focusHints {};
    /// 批间协作取消查询（§12.2"@cancellation 支持批间取消"；与编排器
    /// IEvaluationContext::cancellationRequested 同型语义——TASK-01）。
    /// true ⇒ 生成器返回空批＋cancelled（不产出部分批——生成批是不可分的
    /// 确定性整体，评估期的部分保留语义在编排器候选循环层承载）。
    bool cancellationRequested = false;
};

/**
 * @brief 单次候选批生成的产出面（§12.2"返回空批＋cancelled 标记"的具体化）。
 *
 * 值语义；线程安全：纯值。
 */
struct GenerationBatch {
    /// 候选补丁批。单批策略（本文件两个内置策略）首元素恒为基线空补丁
    /// （§8.2"基线候选……总是生成并参与评估"/OPT-11），label "baseline"；
    /// 后续元素为策略采样/扰动候选。取消时为空批（cancelled=true）。
    std::vector<CandidatePatch> candidates {};
    /// true＝因取消请求提前终止（candidates 为空批——§12.2 取消语义；
    /// 已回传批保留的评估期语义由编排器候选循环承载，生成层不产部分批）。
    bool cancelled = false;
    /// true＝策略已无更多候选（单批策略第 0 代产出后即 exhausted；多代
    /// 推进编排以此判定终止——消费编排归 WP-21-T03）。调用方可据此停止
    /// 后续 generateNextBatch 请求。
    bool exhausted = false;
};

// =====================================================================
// 扩展点接口（§12.1/§12.2——OPT-10 搜索策略接口）
// =====================================================================

/**
 * @brief 候选生成策略扩展点（OPT-10——§12.2 Draft 签名的 C++17 具体化）。
 *
 * 业务定位：优化管线的候选来源唯一经本接口——"注册新 ICandidateGenerator
 * 实现（strategyId 登记＋config.opt 引用）"即 §8.2 明文的扩展方式。实现
 * 契约（§12.2/§12.3 原文约束）：
 *   - strategyId() 返回稳定 token 且 ∈ config.opt 可引用词表（新实现随装配
 *     批次登记词表与校验接受集——单元卡增量修订留痕）；
 *   - generateNextBatch 确定性：同 (ctx, 构造参数) ⇒ 同批（随机性仅来自
 *     ctx.config.seed 的显式种子——§12.3 确定性行）；生成纯函数式推进，
 *     无隐藏状态依赖线程时序（§12.2 @post）；
 *   - 取消：ctx.cancellationRequested==true ⇒ 返回空批＋cancelled
 *     （§12.2 @cancellation"返回空批＋cancelled 标记"）；
 *   - 预算：候选数受 ctx.config.budget 约束（§8.2"生成器受 OptimizationBudget
 *     约束"；maxCandidates 含基线）；
 *   - 编码：候选经 makeCandidatePatch 构造（补丁 canonical 唯一编码层——
 *     §8.2"编码即补丁 canonical 内容，无第二编码层"）；
 *   - 锁定纪律：锁定/未授权绑定不出现在补丁中（§5.4/OPT-02——出现在补丁
 *     即 makeCandidatePatch 拒绝，生成域应预先排除）。
 *
 * 生命周期：实现实例由装配方（L5）或编排依赖注入方持有；本接口不接管
 * 所有权（§12.3 引用有效期行）。线程约束：实现须可重入或由调用方串行化
 * （编排器 run() 为单线程串行消费）。
 */
class ICandidateGenerator {
public:
    virtual ~ICandidateGenerator() = default;

    /**
     * @brief 策略稳定 token（config.opt 引用键——与 OptimizationConfiguration.
     *        strategyId 同一词表；每次调用同值，实现须常量返回）。
     */
    virtual std::string strategyId() const = 0;

    /**
     * @brief 生成下一候选批（确定性——种子/预算/编码均来自 ctx.config）。
     *
     * @param ctx [in] 冻结生成上下文（配置/代序/聚焦提示/取消位）
     * @return 生成批（首元素基线〔单批策略〕；取消时空批＋cancelled；
     *         exhausted 表示策略候选已尽）
     *
     * @throws OptimizationError(kOptInputInvalid) ctx.config 校验拒绝（防御
     *         复验——同 validateConfiguration 拒绝面）；kOptPatchIllegal 研究
     *         定义与词表值域矛盾（补丁构造拒绝——fail-fast，生成期非法＝
     *         研究定义非法，不吞错不跳批）
     */
    virtual GenerationBatch generateNextBatch(const GenerationContext& ctx) = 0;
};

// =====================================================================
// 内置策略一：seeded-lhs 适配器（§8.2 表第一行——WP-20-T06 交付面复用）
// =====================================================================

/**
 * @brief 确定性种子拉丁超立方采样策略（§8.2 表 "opt.strategy.seeded-lhs"）。
 *
 * 实现形态＝**薄适配器**：候选批逐字节委托 generateSeededLhsCandidates
 * （WP-20-T06 落位的 R1 生成函数——单一实现点零复制，既有黄金行为零漂移）。
 * 单批策略：generationIndex==0 产出全批（基线恒首）＋exhausted=true；>0
 * 返回空批＋exhausted。取消位生效（空批＋cancelled）。
 *
 * 为什么适配器而非重写：种子 LHS 的 splitmix64/层置换/采样域收缩逻辑已在
 * EvaluatorPorts 实现单点化并有黄金钉扎测试——第二实现＝第二真相源（违反
 * "编码由 optimization 实现单点承载"纪律）；本类只补接口形态，不改算法。
 */
class SeededLhsGenerator final : public ICandidateGenerator {
public:
    /// 策略 token（＝kStrategySeededLhs——config.opt 引用键）。
    std::string strategyId() const override;

    /**
     * @brief 生成种子 LHS 候选批（委托 generateSeededLhsCandidates 单点）。
     *
     * @param ctx [in] 冻结上下文（config 必填；generationIndex>0 ⇒ 空批；
     *              cancellationRequested ⇒ 空批＋cancelled）
     * @return 生成批（首元素基线；exhausted 恒 true——单批策略）
     *
     * @throws OptimizationError 同 generateSeededLhsCandidates 拒绝面
     *         （kOptInputInvalid 配置非法／kOptPatchIllegal 研究定义矛盾）
     */
    GenerationBatch generateNextBatch(const GenerationContext& ctx) override;
};

// =====================================================================
// 内置策略二：baseline-perturb 邻域扰动（§8.2 表第二行——本批新实现）
// =====================================================================

/**
 * @brief 邻域扰动策略配置面（§8.2 表第二行"步长/比例入 config（确定性）"
 *        的策略侧承载）。
 *
 * 字段边界（诚实登记）：§4.3 config.opt 契约面**零新增字段**（canonical
 * 布局黄金不变）——扰动比例经生成器构造参数（装配期配置面）进入确定性
 * 契约"同 (ctx, 构造参数) ⇒ 同批"；量化变量的步长直接取绑定 step 字段
 * （本就在 config.opt variables 内——"步长入 config"的字面承载）。缓存
 * 一致性：比例不同 ⇒ 生成的补丁值不同 ⇒ 补丁身份不同 ⇒ sliceId 不同
 * （§4.5 失效映射经补丁身份条目自动成立），无需比例入 canonical。
 *
 * 值语义；线程安全：纯值。
 */
struct BaselinePerturbParams {
    /// 连续变量单步扰动比例（Δ＝perturbRatio×该变量行程）；合法域
    /// (0, kBaselinePerturbMaxRatio]（0＝无扰动无意义；>0.04 超附录 C 表①
    /// 冻结放宽档位）。默认＝kBaselinePerturbDefaultRatio（冻结值）。
    double perturbRatio = kBaselinePerturbDefaultRatio;
};

/**
 * @brief 校验邻域扰动配置（fail-fast——调用方错误抛 OptimizationError）。
 *
 * 规则（不静默修正——NFR-COR-03）：perturbRatio 必须有限且
 * 0 < perturbRatio ≤ kBaselinePerturbMaxRatio（0.04——附录 C 表①冻结放宽
 * 上界；NaN/Inf/0/负值/超档位一律拒绝，消息含实际值与合法域）。
 *
 * @param params [in] 待校验配置
 * @throws OptimizationError(kOptInputInvalid) 任一违例
 *
 * 纯函数；线程安全（可重入）。
 */
void validateBaselinePerturbParams(const BaselinePerturbParams& params);

/**
 * @brief 基线邻域扰动策略（§8.2 表第二行——改型场景的利用先验策略）。
 *
 * 算法（单因子 ±Δ 邻域枚举——坐标搜索式确定性 OFAT 轮转；无随机自由度）：
 *   1. 防御复验：validateConfiguration(ctx.config)＋本策略参数校验。
 *   2. 取消位：ctx.cancellationRequested ⇒ 空批＋cancelled。
 *   3. 代序：generationIndex>0 ⇒ 空批＋exhausted（单批策略——邻域在第 0
 *      代已完整枚举）。
 *   4. 扰动维度集：config.variables 中**未锁定且授权**（§8.2"锁定集外扰动"/
 *      OPT-02）且值类别为 Continuous/Quantized 的绑定，按 bindingId 字典序
 *      （确定性维度序）。**枚举与离散器件维度不在本策略扰动域**（登记：
 *      枚举/器件无连续邻域语义——±Δ 无定义；其组合空间探索＝seeded-lhs
 *      均匀采样面与 §5.6 R2 离散采样，同 §8.6 模式③排除口径一致）。维度集
 *      可以为空——批退化为仅基线（如实不伪造多样性）。
 *   5. 批首恒为基线空补丁（§8.2/OPT-11）。
 *   6. 单步扰动幅度 Δ_v（逐维度）：Quantized＝绑定 step 字段（"步长入
 *      config"的字面承载——网格邻点）；Continuous＝perturbRatio×(upper−lower)
 *      （"比例入 config"——装配期配置面，默认 0.01＝附录 C 表①冻结值）。
 *   7. 邻域调度（确定性轮转）：第 i 个邻域候选取维度 v＝i mod m、方向
 *      sign＝(i/m)==0 ? +1 : −1（先正向轮转全部维度，再负向轮转——每维度
 *      的 ±Δ 双向在预算充足时必然各出现一次）；候选值＝defaultValue＋
 *      sign×Δ_v。**合法性预检**：候选值须有限且落在 [lowerBound,upperBound]
 *      界内，且 valueMustBePositive 词表硬约束位（I-MDL-11）维度须 >0——
 *      不合法方向**跳过不生成**（邻域枚举语义即"合法邻点"；越界方向不是
 *      邻点，跳过是采样设计而非静默纠错——登记）；调度在收集满 n 个或
 *      i 达 2m（全部 (维度,方向) 对耗尽）时终止。
 *   8. 每候选经 makeCandidatePatch 构造**单项补丁**（OFAT 单因子——候选
 *      恰含一个绑定项；补丁 canonical 唯一编码层，量化对齐/排序/规范化
 *      复用 T03 单点）。构造抛出（如 defaultValue 越界/正性矛盾——研究
 *      定义非法）fail-fast 传播，不跳过不吞。
 *   9. 非基线候选数上限 n＝budget.maxCandidates−1（§8.2 预算约束）；邻域
 *      总量 2m<n 时批短于预算，exhausted=true（邻域已尽）。
 *
 * 确定性：同 (ctx, params) ⇒ 同批（逐字节——调度无随机项＋补丁构造单点）；
 * 种子变化不改变本策略批（无随机自由度），但改变 config.opt 身份（重算）。
 *
 * 与 seeded-lhs 的分工（§8.2 两行语义）：本策略＝基线**邻域**（利用先验，
 * 改型初始化形态）；seeded-lhs＝**全局**空间填充（探索优先，新机型初始化
 * 形态）——多因子组合探索不在本策略（单因子 OFAT），由 seeded-lhs 承载。
 */
class BaselinePerturbGenerator final : public ICandidateGenerator {
public:
    /**
     * @brief 构造（策略配置面定型——构造后只读）。
     *
     * @param params [in] 扰动配置（默认＝冻结比例 0.01）；**构造期不校验**
     *              （校验在 generateNextBatch 入口统一执行——轻量对象允许
     *              先构造后配置的测试装配形态；运行前必经校验单点）
     */
    explicit BaselinePerturbGenerator(BaselinePerturbParams params = {});

    /// 策略 token（＝kStrategyBaselinePerturb——config.opt 引用键）。
    std::string strategyId() const override;

    /**
     * @brief 生成基线邻域扰动候选批（算法见类注——OFAT ±Δ 确定性枚举）。
     *
     * @param ctx [in] 冻结上下文（config 必填；generationIndex>0 ⇒ 空批；
     *              cancellationRequested ⇒ 空批＋cancelled）
     * @return 生成批（首元素基线；邻域耗尽时 exhausted=true）
     *
     * @throws OptimizationError(kOptInputInvalid) 配置/策略参数非法；
     *         kOptPatchIllegal 补丁构造拒绝（研究定义与词表值域矛盾——
     *         fail-fast）
     */
    GenerationBatch generateNextBatch(const GenerationContext& ctx) override;

private:
    BaselinePerturbParams m_params; ///< 扰动配置（构造定型，运行期只读）
};

// =====================================================================
// 内置词表聚合与分派单点（validateConfiguration ⑨ 与编排器 run() 同源）
// =====================================================================

/**
 * @brief 已实现内置策略词表（§8.2 表的机器可断言面——NFR-MNT-04"只登记
 *        已实现"的执行点）。
 *
 * 返回序＝卡面 §8.2 表行序（seeded-lhs→baseline-perturb——确定性）；每个
 * token 都有真实实现类（SeededLhsGenerator/BaselinePerturbGenerator）且可经
 * generateWithBuiltInStrategy 解析（契约测试逐 token 解析钉住——"登记而
 * 不可解析"即空占位违约）。validateConfiguration 检查⑨以本函数为接受集
 * 单点；新策略随实现批次在此追加登记（单元卡增量修订留痕）。
 *
 * @return 词表只读引用（内部静态表——进程生存期；调用方不得修改/释放）。
 *
 * 线程安全（只读共享，可重入）。
 */
const std::vector<std::string_view>& builtInStrategyIds();

/**
 * @brief 按内置词表分派生成（词表→实现的唯一分派点）。
 *
 * 消费面：编排器 run()（deps.generator 为空时的内置路径）与测试（词表
 * 完备性/零占位断言——逐 token 可解析即有真实现）。与 validateConfiguration
 * 检查⑨同源（同用 builtInStrategyIds）——"校验接受的 token 必可分派、
 * 可分派的 token 必被校验接受"，两表合一杜绝词表漂移。
 *
 * @param strategyId    [in] 策略 token（须 ∈ builtInStrategyIds()）
 * @param ctx           [in] 冻结生成上下文（透传所选策略）
 * @param perturbParams [in] baseline-perturb 策略配置面（其余策略忽略——
 *                      缺省＝冻结比例 0.01）
 * @return 生成批（所选策略的 generateNextBatch 输出）
 *
 * @throws OptimizationError(kOptInputInvalid) strategyId 未登记（消息含
 *         已实现词表全文——ERR-01 定位纪律）；所选策略生成面的原生拒绝面
 *         原样传播
 *
 * 纯函数；线程安全（可重入）。
 */
GenerationBatch generateWithBuiltInStrategy(
    const std::string& strategyId, const GenerationContext& ctx,
    const BaselinePerturbParams& perturbParams = {});

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_GENERATOR_HPP
