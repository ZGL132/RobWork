/**
 * @file   JsonLiteTest.cpp
 * @brief  受限 JSON 用例组——TK-JSON（units/testkit.md §8）：合法往返/键序稳定/
 *         拒绝清单逐项/行列与字段路径定位。
 *
 * 设计依据：
 *   - units/testkit.md §4.1（受限范围与拒绝清单）、§8 TK-JSON 行、D-02（边界声明）
 *   - 需求 NFR-COR-01（数据载体）；任务契约 tasks/foundation/TK-T02.json
 *     acceptance 三条（往返/拒绝全过、行列＋字段路径可定位、D-02 边界）
 *
 * 确定性口径：dump(parse(canonical)) == canonical（canonical 文本＝紧凑形态、
 * 数字为最短往返表示）；dump 幂等（parse→dump→parse→dump 稳定）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/JsonLite.hpp>

#include <stdexcept>
#include <string>

namespace {
using namespace sdurws::ird::testkit;

/// 解析失败断言辅助：必须抛 TestKitError，且消息含指定行:列与路径片段。
/// lineCol 形如 "3:8"（行:列）——内部转换为错误消息的 "line 3, column 8" 口径。
void expectParseFail(const std::string& text, const std::string& lineCol,
                     const std::string& pathFragment = "$")
{
    const auto colon = lineCol.find(':');
    const std::string needle =
        "line " + lineCol.substr(0, colon) + ", column " + lineCol.substr(colon + 1);
    try {
        (void)parseJson(text);
        FAIL() << "必须拒绝: " << text;
    } catch (const TestKitError& e) {
        const std::string what = e.what();
        EXPECT_EQ(what.find("dataset-invalid: json-parse: "), 0u)
            << "错误前缀/分类不符: " << what;
        EXPECT_NE(what.find(needle), std::string::npos)
            << "行列定位缺失（期望 " << needle << "）: " << what;
        EXPECT_NE(what.find(pathFragment), std::string::npos)
            << "字段路径缺失: " << what;
    }
}

/** 合法往返：canonical 文本 parse→dump 逐字符还原（§8"dump(parse(x))==x"）。 */
TEST(JsonRoundtrip, CanonicalTextRestored_UT_JSON)
{
    // 覆盖六类值：对象（嵌套）/数组/字符串（转义＋非 ASCII 透传）/数字（负/零/
    // 小数/整数）/true/false/null。书写即为 canonical 形态（数字＝to_chars 最短
    // 表示——整数不带小数点；转义仅 \n \t \"）。raw string 内 \" \n 即字面反斜杠序列。
    const std::string canonical =
        R"({"id":"kin-fk-planar-2r","v":2,"ratio":0.5,"neg":-3,"exp":1500,"ok":true,"off":false,"none":null,"path":{"points":[{"x":-0.25,"y":0}]},"esc":"a\nb\t\"q\""})";
    const JsonValue parsed = parseJson(canonical);
    EXPECT_EQ(dumpJson(parsed, false), canonical);

    // 幂等：parse→dump→parse→dump 稳定（§4.1 确定性输出）。
    const std::string once = dumpJson(parsed, false);
    EXPECT_EQ(dumpJson(parseJson(once), false), once);

    // pretty 形态语义等价（deepEquals：对象键序无关）且再解析稳定。
    const std::string pretty = dumpJson(parsed, true);
    EXPECT_TRUE(parseJson(pretty).deepEquals(parsed));
}

/** 键序＝插入序：{b, a} 按书写顺序输出，不排序（§4.1 明文）。 */
TEST(JsonRoundtrip, InsertionOrderStable_UT_JSON)
{
    const JsonValue v = parseJson(R"({"b":1,"a":2})");
    ASSERT_TRUE(v.isObject());
    ASSERT_EQ(v.members.size(), 2u);
    EXPECT_EQ(v.members[0].first, "b");             // 插入序：b 在前
    EXPECT_EQ(v.members[1].first, "a");
    EXPECT_EQ(dumpJson(v, false), R"({"b":1,"a":2})");   // 不重排
}

/** 值模型访问：类型判别/find 命中与未命中/数组索引（TK-T03 装载面的消费形态）。 */
TEST(JsonAccess, KindDiscriminatorsAndFind_UT_JSON)
{
    const JsonValue v = parseJson(R"({"name":"manifest","files":["a.json","b.json"],"n":3})");
    ASSERT_TRUE(v.isObject());
    ASSERT_NE(v.find("name"), nullptr);
    EXPECT_TRUE(v.find("name")->isString());
    EXPECT_EQ(v.find("name")->text, "manifest");

    const JsonValue* files = v.find("files");
    ASSERT_NE(files, nullptr);
    ASSERT_TRUE(files->isArray());
    ASSERT_EQ(files->items.size(), 2u);
    EXPECT_EQ(files->items[1].text, "b.json");

    EXPECT_EQ(v.find("missing"), nullptr);          // 未命中不抛（装载层决定缺失语义）
    EXPECT_TRUE(v.find("name")->find("x") == nullptr);   // 非对象 find 安全返回空

    const JsonValue num = parseJson("-0.25");
    ASSERT_TRUE(num.isNumber());
    EXPECT_EQ(num.number, -0.25);
}

/** 拒绝清单（§4.1）：重复键/NaN/Infinity/尾随内容/注释/BOM——全部给行列与路径。 */
TEST(JsonRejections, RejectionMatrix_UT_JSON)
{
    expectParseFail(R"({"a":1,"a":2})", "1:8", "$.a");                    // 重复键：第二键开引号在 col 8，路径 $.a
    expectParseFail("{\"a\":1,\"b\":{\"c\":3,\"c\":4}}", "1:19", "$.b.c"); // 嵌套重复键（外层无重复）：路径 $.b.c
    expectParseFail("[NaN]", "1:2", "$[0]");                              // NaN 字面
    expectParseFail("[Infinity]", "1:2", "$[0]");                         // Infinity 字面
    expectParseFail("[-Infinity]", "1:3", "$[0]");                        // -Infinity 字面（'-' 后的 I 处）
    expectParseFail("{\"a\":1} trailing", "1:9", "$");                    // 尾随内容
    expectParseFail("// comment\n{}", "1:1", "$");                        // 行注释
    expectParseFail("/* block */{}", "1:1", "$");                         // 块注释
    const std::string withBom = "\xEF\xBB\xBF{}";                         // UTF-8 BOM
    expectParseFail(withBom, "1:1", "$");
}

/** 拒绝清单（语法越界）：前导零/尾逗号/未闭合/非法转义/控制字符/空输入/溢出。 */
TEST(JsonRejections, GrammarViolations_UT_JSON)
{
    expectParseFail("01", "1:2");                         // 前导零（'0' 后的 '1' 处）
    expectParseFail("[1,2,]", "1:6", "$");                // 数组尾逗号（']' 处）
    expectParseFail(R"({"a":1,})", "1:8", "$");           // 对象尾逗号
    expectParseFail(R"({"a":1)", "1:7", "$");             // 对象未闭合（结构错误归对象层，路径 $）
    expectParseFail("[1,", "1:4", "$");                   // 数组未闭合
    expectParseFail(R"("unterminated)", "1:14");          // 字符串未闭合（EOF 处）
    expectParseFail(R"("bad\x")", "1:7");                 // 非法转义（x 处）
    expectParseFail(std::string("\"") + char(0x01) + "\"", "1:2");   // 裸控制字符
    expectParseFail("   \n  ", "2:3");                    // 空输入（纯空白）：一个换行后行 2 列 3
    expectParseFail("1e999", "1:1");                      // 溢出为非有限（按数字起始位置定位）
    expectParseFail(R"({"a" 1})", "1:6", "$.a");          // 缺冒号
    expectParseFail(R"({"a":1 "b":2})", "1:8", "$");      // 缺逗号
}

/** 行列定位精度：多行文本中错误落在第 3 行第 8 列（acceptance 第 2 条的载体）。 */
TEST(JsonErrorLocation, LineColumnPrecision_UT_JSON)
{
    // 第 1、2 行合法；第 3 行 "b": NaN——N 在该行第 8 列。
    const std::string multi = "{\n  \"a\": 1,\n  \"b\": NaN\n}";
    expectParseFail(multi, "3:8", "$.b");
}

/** Unicode：\uXXXX（BMP）与代理对（增补平面）还原为 UTF-8 字节（§4.1 标准转义）。 */
TEST(JsonUnicode, EscapesToUtf8_UT_JSON)
{
    const JsonValue bmp = parseJson(R"("\u0041\u4e2d")");
    EXPECT_EQ(bmp.text, std::string("A中"));        // 单字节＋三字节 UTF-8

    const JsonValue sur = parseJson(R"("\uD83D\uDE00")");
    EXPECT_EQ(sur.text, std::string("\xF0\x9F\x98\x80"));   // U+1F600 四字节

    expectParseFail(R"("\uD83D")", "1:8");          // 孤立高代理拒绝
    expectParseFail(R"("\u00")", "1:6");            // 不足 4 位拒绝
}

/** D-02 边界的代码侧留痕：测试数据出口只写 testdata 语义值（文档化断言——
 *  JsonLite 的 dump 输出可被再次解析且语义不变，不引入第二持久化格式声明）。 */
TEST(JsonBoundary, DumpOutputReparseable_UT_JSON)
{
    const JsonValue v = parseJson(R"({"suite":"core","cases":[1,2,3]})");
    const std::string text = dumpJson(v, true);
    EXPECT_TRUE(parseJson(text).deepEquals(v));     // 人读形态与紧凑形态语义等价
}
}  // namespace
