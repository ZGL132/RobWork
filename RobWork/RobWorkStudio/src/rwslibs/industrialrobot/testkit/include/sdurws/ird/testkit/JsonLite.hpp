/**
 * @file   JsonLite.hpp
 * @brief  JsonLite——测试侧受限 JSON：值模型、严格解析（行列定位）与确定性序列化。
 *
 * 设计依据：
 *   - units/testkit.md §4.1（范围/拒绝清单/接口签名/自带理由）、§8 TK-JSON（往返/
 *     拒绝用例矩阵）、D-02（不用于产品格式）、§5/§7.2（TestKitError 分类与助记码）
 *   - 需求 NFR-COR-01（黄金数据载体——manifest/容差档案的文本形式）
 *   - 任务契约 tasks/foundation/TK-T02.json（TK-T02 acceptance 三条）
 *
 * 背景说明（§4.1"为何自带"）：io 的 JSON 读写器属产品单元且未产出（依赖 io 违反
 * T-2）；vcpkg 无现成独立 JSON 库（引入第三方超边界）；RobWork 无可借头。
 *
 * ★ 边界声明（D-02，验收第 3 条的载体）：JsonLite 是测试工具，不构成产品持久化
 * 格式的第二个实现点——其输出只入 testdata/ 与测试报告；若 DTB 未来统一测试侧
 * JSON 机制，本单元按增量修订对齐（P-TK-6）。
 *
 * 受限范围（§4.1 原文）：对象/数组/字符串（UTF-8，标准转义）/数字（IEEE double
 * 可精确解析区间）/true/false/null；拒绝：重复键、NaN/Infinity 字面、尾随内容、
 * 注释、BOM、前导零/孤立符号/尾逗号等语法越界；错误消息含行/列与字段路径。
 *
 * 确定性（NFR-COR-02 精神）：对象键序＝插入序（不排序）；数字经 std::to_chars
 * 最短往返表示输出——同值同串，dump(parse(dump(v))) 与 dump(v) 逐字符一致。
 *
 * 线程安全：JsonValue 为纯值类型；parseJson/dumpJson 无共享状态（各自独立）。
 */

#ifndef SDURWS_IRD_TESTKIT_JSONLITE_HPP
#define SDURWS_IRD_TESTKIT_JSONLITE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/testkit/TestPaths.hpp>   // TestKitError（前置定义，变更记录 v0.3）

namespace sdurws::ird::testkit {

/**
 * @brief 受限 JSON 值模型：null/bool/double/string/array/object 五类七态。
 *
 * 表示取舍：对象用"键值对向量"而非映射——保插入序（§4.1 键序＝插入序，
 * 确定性输出）且测试规模下线性查找足够；数组用向量。
 * 拷贝/移动＝深拷贝/转移（纯值语义，无共享句柄）；线程安全（不可变共享安全）。
 */
struct JsonValue {
    /// 值类别（§4.1 范围五类；bool 与 null 分列以便类型化访问）。
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;                                  ///< 当前类别（默认 null）

    // ---- 分类存储（同一时刻仅 kind 对应成员有效；其余为默认态） ----
    bool boolean = false;                                    ///< kind==Bool 时有效
    double number = 0.0;                                     ///< kind==Number 时有效（IEEE double）
    std::string text;                                        ///< kind==String 时有效（UTF-8，转义已还原）
    std::vector<JsonValue> items;                            ///< kind==Array 时有效（有序）
    std::vector<std::pair<std::string, JsonValue>> members;  ///< kind==Object 时有效（键序＝插入序）

    // ---- 类型判别（noexcept 纯值查询） ----
    bool isNull() const noexcept { return kind == Kind::Null; }
    bool isBool() const noexcept { return kind == Kind::Bool; }
    bool isNumber() const noexcept { return kind == Kind::Number; }
    bool isString() const noexcept { return kind == Kind::String; }
    bool isArray() const noexcept { return kind == Kind::Array; }
    bool isObject() const noexcept { return kind == Kind::Object; }

    /**
     * @brief 对象按键查找（线性；键序无关）。
     *
     * @param key [in] 目标键名（区分大小写，精确匹配）
     * @return 命中返回指向该值的指针（生命周期随本对象）；未命中返回 nullptr——
     *         不抛错（调用方决定缺失语义，服务 §4.2 清单装载的逐字段校验）
     */
    const JsonValue* find(std::string_view key) const noexcept;

    /**
     * @brief 深相等：类型与内容递归全等（数字按位比较——确定性口径，不做数值容差）。
     *
     * 对象相等不要求键序一致（语义等价）；数组相等要求元素逐一相等（有序语义）。
     */
    bool deepEquals(const JsonValue& o) const;
    bool operator==(const JsonValue& o) const { return deepEquals(o); }
    bool operator!=(const JsonValue& o) const { return !deepEquals(o); }
};

/**
 * @brief 严格解析受限 JSON 文本（§4.1 唯一解析入口）。
 *
 * 拒绝清单（全部抛 TestKitError(DatasetInvalid)，消息含 "json-parse:" 前缀＋
 * 行:列＋字段路径）：重复键、NaN/Infinity 字面、尾随内容、注释（// 与 /**）×、
 * BOM、前导零/孤立正号、尾逗号、未闭合结构、字符串内控制字符/非法转义、
 * 数字溢出为非有限值、嵌套超过 200 层、空输入。
 *
 * 行列口径：行自 1 计、列自 1 计、按字节偏移回溯（UTF-8 多字节字符按其首字节计列）。
 *
 * @param text [in] UTF-8 编码 JSON 文本（整段；解析器不做流式）
 * @return 解析后的值模型
 *
 * @throws TestKitError 任何上述违例（DatasetInvalid——数据资产缺陷分类）
 *
 * 线程安全：无共享状态，可并发调用。
 */
JsonValue parseJson(std::string_view text);

/**
 * @brief 序列化值模型为 JSON 文本（§4.1：键序＝插入序，确定性输出）。
 *
 * 数字输出：std::to_chars 最短往返表示（整数 double 不带小数点；保证
 * parse(dump(v)) 的数字按位还原）。字符串转义：'"'、'\\' 与控制字符
 * （\b \f \n \r \t 短形式，其余 \u00XX）；非 ASCII UTF-8 字节原样透传。
 *
 * @param value  [in] 待序列化的值
 * @param pretty [in] true＝两空格缩进＋换行的人读形态；false＝紧凑单行
 * @return JSON 文本（UTF-8；无 BOM、无尾随换行）
 *
 * @throws TestKitError(Usage) value.kind 与存储不一致（不可能经由公共接口构造——
 *         防御性 fail-fast）
 *
 * 线程安全：无共享状态。
 */
std::string dumpJson(const JsonValue& value, bool pretty);

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_JSONLITE_HPP
