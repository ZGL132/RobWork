/**
 * @file ProjectBoundaryTest.cpp
 * @brief T01事务审计探测，复用原TxEngine夹具，验证PM-08和NFR-REL-01边界。
 * 原套件会被注册，本批仅过滤新增用例；不修改产品或原测试预期。
 */
#include IRD_PROJECT_EXISTING_TEST
#include <sdurws/ird/project/ProjectStore.hpp>

namespace {
// 复用原夹具的合法磁盘种子、注入接缝与生命周期，不另建前置生成器。
using ReuseProjectBoundary = TxEngineTest;

TEST_F(ReuseProjectBoundary, HealthyStoreRemainsVerified) {
    // PM-08正例：健康种子必须通过，排除损坏夹具导致的假反例。
    EXPECT_TRUE(m_engine->scanForRecovery().headIntegrityVerified);
}

TEST_F(ReuseProjectBoundary, WrongHeadManifestDigestMustBeRejected) {
    // project §7.4④：仅改变HEAD中语法合法的摘要，不改变闭包和对象字节。
    auto head = m_seed.head;
    head.manifestDigest = std::string(64, '0');
    ASSERT_NE(head.manifestDigest, m_seed.head.manifestDigest);
    ASSERT_TRUE(writeRaw(m_proj / "HEAD", sdurws::ird::project::codec::dump(head)));
    EXPECT_FALSE(m_engine->scanForRecovery().headIntegrityVerified);
}

TEST_F(ReuseProjectBoundary, ParseableManifestByteChangeMustBeRejected) {
    // 仅改合法时间值：JSON仍可解码，HEAD摘要仍指原字节，解析通过不等于哈希通过。
    auto manifest = m_seed.r0Manifest;
    manifest.committedAtUtc = "2026-09-16T00:00:01Z";
    const auto bytes = sdurws::ird::project::codec::dump(manifest);
    ASSERT_NE(cvHex(contentVersionOf(bytes)), m_seed.head.manifestDigest);
    ASSERT_TRUE(writeRaw(m_proj / "revisions" / m_seed.r0.toCanonical() / "manifest.json", bytes));
    EXPECT_FALSE(m_engine->scanForRecovery().headIntegrityVerified);
}

TEST_F(ReuseProjectBoundary, NativeDiskFullMustKeepStableErrorClass) {
    // NFR-REL-01：使用Windows SDK真实常量，不能以两端相同的错误数字自证映射。
    const auto before = snapshotCommitted();
    m_ops->failAt(fp::kWriteChunk, 1, ERROR_DISK_FULL);
    const auto plan = makePlan();
    try {
        m_engine->commit(plan, m_engine->readHead(), m_seed.m0, m_seed.m0Ref);
        FAIL() << "磁盘满注入必须拒绝提交";
    } catch (const StoreError& error) {
        EXPECT_EQ(error.code(), StoreErrorCode::DiskFull);
    }
    expectCommittedUnchanged(before);
}

// 公共工厂探测补上project.json，沿用原夹具的其余合法种子；只读打开避免写锁影响。
TEST_F(ReuseProjectBoundary, PublicOpenHealthyStoreSucceeds) {
    sdurws::ird::project::ProjectStaticIdentity identity;
    identity.projectId = m_seed.projectId;
    identity.createdAtUtc = "2026-09-16T00:00:00Z";
    identity.createdWithToolVersion = "reuse-audit";
    ASSERT_TRUE(writeRaw(m_proj / "project.json", sdurws::ird::project::codec::dump(identity)));
    sdurws::ird::project::OpenStoreRequest request;
    request.path = m_proj;
    request.mode = sdurws::ird::project::OpenMode::ReadOnly;
    const auto opened = sdurws::ird::project::ProjectStoreFactory::open(request);
    EXPECT_NE(nullptr, opened.store);
    EXPECT_TRUE(opened.recovery.headIntegrityVerified);
}

TEST_F(ReuseProjectBoundary, PublicOpenWrongHeadDigestMustBeRejected) {
    sdurws::ird::project::ProjectStaticIdentity identity;
    identity.projectId = m_seed.projectId;
    identity.createdAtUtc = "2026-09-16T00:00:00Z";
    identity.createdWithToolVersion = "reuse-audit";
    ASSERT_TRUE(writeRaw(m_proj / "project.json", sdurws::ird::project::codec::dump(identity)));
    auto head = m_seed.head;
    head.manifestDigest = std::string(64, '0');
    ASSERT_NE(head.manifestDigest, m_seed.head.manifestDigest);
    ASSERT_TRUE(writeRaw(m_proj / "HEAD", sdurws::ird::project::codec::dump(head)));
    sdurws::ird::project::OpenStoreRequest request;
    request.path = m_proj;
    request.mode = sdurws::ird::project::OpenMode::ReadOnly;
    // PM-02/08：工厂不得把扫描漏检的摘要失配公开为成功打开。
    try {
        const auto opened = sdurws::ird::project::ProjectStoreFactory::open(request);
        FAIL() << "摘要失配仍成功打开，headIntegrityVerified=" << opened.recovery.headIntegrityVerified;
    } catch (const StoreError& error) {
        EXPECT_EQ(error.code(), StoreErrorCode::StoreCorrupt);
    }
}
} // namespace
