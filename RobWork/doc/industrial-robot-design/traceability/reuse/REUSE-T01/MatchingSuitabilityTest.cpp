/**
 * @file MatchingSuitabilityTest.cpp
 * @brief REUSE-T01：既有Boost最大匹配候选与原core数值边界的隔离探测。
 * 依据：audit-contract §2/§6；复用原GTest/testkit与生产core目标，不改产品。
 * 仅核候选API及列明输入；不能作为生产适配、全部集合语义或性能通过证据。
 */
#include <gtest/gtest.h>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/max_cardinality_matching.hpp>
#include <sdurws/ird/core/Compare.hpp>
#include <limits>
#include <vector>

namespace {
/// 四顶点二分图，左点0/1、右点2/3；无自持增广算法，所有求解委托Boost。
using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS>;
using Vertex = boost::graph_traits<Graph>::vertex_descriptor;
}

// TK-SET：贪心先占(0,2)会卡住左点1；成熟最大匹配须找(0,3)+(1,2)。
TEST(ReuseAuditMatching, BoostMaximumMatchingAvoidsGreedyTrap) {
    Graph graph(4);
    boost::add_edge(0, 2, graph);
    boost::add_edge(0, 3, graph);
    boost::add_edge(1, 2, graph);
    std::vector<Vertex> mate(4);
    const auto map = boost::make_iterator_property_map(mate.begin(), boost::get(boost::vertex_index, graph));
    ASSERT_TRUE(boost::checked_edmonds_maximum_cardinality_matching(graph, map));
    EXPECT_EQ(2u, boost::matching_size(graph, map));
    EXPECT_EQ(3u, mate[0]);
    EXPECT_EQ(2u, mate[1]);
}

// TK-SET/F-594：公开求解器可核删边后仍完美的真歧义，与唯一匹配作对照。
TEST(ReuseAuditMatching, BoostMatchingDistinguishesAlternativeAfterRemovingEdge) {
    for (const bool ambiguous : {false, true}) {
        Graph graph(4);
        boost::add_edge(0, 2, graph);
        boost::add_edge(1, 3, graph);
        if (ambiguous) {
            boost::add_edge(0, 3, graph);
            boost::add_edge(1, 2, graph);
        }
        std::vector<Vertex> mate(4);
        const auto map = boost::make_iterator_property_map(mate.begin(), boost::get(boost::vertex_index, graph));
        ASSERT_TRUE(boost::checked_edmonds_maximum_cardinality_matching(graph, map));
        ASSERT_EQ(2u, boost::matching_size(graph, map));
        boost::remove_edge(0, mate[0], graph);
        ASSERT_TRUE(boost::checked_edmonds_maximum_cardinality_matching(graph, map));
        EXPECT_EQ(ambiguous ? 2u : 1u, boost::matching_size(graph, map));
    }
}

// C4/NFR-COR-03：有限输入的减法与容差限都溢出时，不能以inf<=inf伪造接近。
// 以最大有限数M为单位，差为2M，阈值为0.5M+M=1.5M，应拒绝；缩放oracle只比较2与1.5。
// 本用例先保留原生产实现的真实失败，后续T05修复不得放宽需求或移除反例。
TEST(ReuseAuditCoreBoundary, C4FiniteOverflowMustNotAcceptOutOfTolerance) {
    const double max = std::numeric_limits<double>::max();
    ASSERT_GT(2.0, 0.5 + 1.0);
    EXPECT_FALSE(sdurws::ird::core::closeWithin(
        max, -max, sdurws::ird::core::Tolerance::make(0.5, max)));
}
