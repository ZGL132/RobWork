/**
 * @file DiagnosticSchemaSuitabilityTest.cpp
 * @brief REUSE-T01/B33：受限参数文法、现有JSON候选差异和去重键最小探测。
 *
 * 依据：diagnostics ParamSchema.hpp的失败输出契约、units/diagnostics.md
 * §4.5/§6.4；只消费真实私有辅助和同渠道已安装Boost，不新增解析核心。
 */
#include <gtest/gtest.h>
#include <boost/property_tree/json_parser.hpp>
#include <sdurws/ird/core/DiagData.hpp>
#include "ParamSchema.hpp"
#include "EntryDetail.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace core = sdurws::ird::core;
namespace diag = sdurws::ird::diagnostics;

/** 失败输出契约反例：曾读入一个合法参数后失败，也必须清空names。 */
TEST(DiagnosticSchemaAudit, EveryFailureMustClearNames_DT_REG_5)
{
    for (const std::string schema : {"[\"pid\",\"PID\"]", "[\"pid\",\"pid\"]",
                                     "[\"pid\",]", "[\"pid\"]x"}) {
        SCOPED_TRACE(schema);
        std::vector<std::string> names{"old"};
        std::string error = "old";
        EXPECT_FALSE(diag::detail::tryParseParamSchema(schema, &names, &error));
        EXPECT_TRUE(names.empty());
        EXPECT_FALSE(error.empty());
    }
}

/** 已接受子集需保持声明顺序、成功清错；不扩展冻结的词形/空白政策。 */
TEST(DiagnosticSchemaAudit, AcceptedSubsetPreservesOrder_DT_REG_5)
{
    std::vector<std::string> names;
    std::string error = "old";
    ASSERT_TRUE(diag::detail::tryParseParamSchema(" [\"pid\",\"host-name\",\"v2\"]\t", &names, &error));
    EXPECT_EQ(names, (std::vector<std::string>{"pid", "host-name", "v2"}));
    EXPECT_TRUE(error.empty());
    ASSERT_TRUE(diag::detail::tryParseParamSchema("[]", &names, &error));
    EXPECT_TRUE(names.empty());
}

/** PropertyTree的JSON树擦除字符串/数字类型，不能直接承担冻结字符串数组文法。 */
TEST(DiagnosticSchemaAudit, ExistingPropertyTreeErasesJsonScalarTypes)
{
    boost::property_tree::ptree strings;
    boost::property_tree::ptree numbers;
    std::istringstream stringInput("[\"1\"]");
    std::istringstream numberInput("[1]");
    boost::property_tree::read_json(stringInput, strings);
    boost::property_tree::read_json(numberInput, numbers);
    EXPECT_EQ(strings, numbers);
    std::vector<std::string> names;
    std::string error;
    EXPECT_TRUE(diag::detail::tryParseParamSchema("[\"1\"]", &names, &error));
    EXPECT_FALSE(diag::detail::tryParseParamSchema("[1]", &names, &error));
}

/** 通用JSON解转义和换行空白与受限文法不同；不能只做树上参数名校验。 */
TEST(DiagnosticSchemaAudit, ExistingPropertyTreeAcceptsExcludedLexicalForms)
{
    for (const std::string schema : {"[\"\\u0070id\"]", "[\n\"pid\"\n]"}) {
        boost::property_tree::ptree tree;
        std::istringstream input(schema);
        ASSERT_NO_THROW(boost::property_tree::read_json(input, tree));
        ASSERT_EQ(tree.size(), 1u);
        EXPECT_EQ(tree.begin()->second.data(), "pid");
        std::vector<std::string> names;
        std::string error;
        EXPECT_FALSE(diag::detail::tryParseParamSchema(schema, &names, &error));
    }
}

/** 去重键是业务身份投影：跨对象/跨任务不得合并，不是一般容器哈希服务。 */
TEST(DiagnosticSchemaAudit, ExistingDedupKeyKeepsObjectAndTaskIsolation_DT_DUP_2)
{
    const auto first = core::ObjectId::generate();
    const auto second = core::ObjectId::generate();
    const auto record = core::DiagnosticRecord::make("RT-AUDIT", first, {}, {},
                                                    "context", "cause", "action");
    auto otherRecord = record;
    otherRecord.subject = second;
    diag::DiagContext context;
    const auto key = diag::detail::deriveDedupKey(record, context);
    const auto otherKey = diag::detail::deriveDedupKey(otherRecord, context);
    EXPECT_NE(key, otherKey);
    EXPECT_EQ(key.scopeKind, diag::ScopeKind::Object);
    core::TaskIdentity task{core::ProjectId::generate(), core::BranchId::generate(),
                            core::RevisionId::generate(), core::RunId::generate(),
                            core::AttemptId::fromCanonical("att-1")};
    context.task = task;
    const auto taskKey = diag::detail::deriveDedupKey(record, context);
    context.task->attempt = core::AttemptId::fromCanonical("att-2");
    EXPECT_NE(taskKey, diag::detail::deriveDedupKey(record, context));
    EXPECT_EQ(taskKey.scopeKind, diag::ScopeKind::Task);
    EXPECT_EQ(diag::detail::codeLower("RT-AUDIT-01"), "rt-audit-01");
}
} // namespace
