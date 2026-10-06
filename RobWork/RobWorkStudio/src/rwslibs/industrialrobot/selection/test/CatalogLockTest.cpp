/**
 * @file   CatalogLockTest.cpp
 * @brief  目录版本锁定用例组（SelCatalogLock）——写入即锁定只增、锁定后
 *         不可变、引用完整性、内容身份与历史隔离（AT-08"版本锁定"用例面；
 *         卡 §4.2 快照纪律＋§14.1 供给契约）。
 *
 * 设计依据：
 *   - units/selection.md §4.2（目录数据冻结——导入成功→不可变快照→写入
 *     即锁定只增；不完整导入＝删除重导；历史选型结果继续引用原目录版本
 *     对象，目录更新不静默改变历史结果；内容身份变更→切片失效）、
 *     §4.3（身份三元组——(catalogId, version, 包内容摘要)）、§14.1
 *     （ICatalogProvider——load 摘要不符/对象不存在 fail-fast）
 *   - 需求 SEL-01（版本化目录包）、SEL-08（项目锁定版本——本域执行面；
 *     project 端口形态随 WP-19-T07）、CON-02/CON-05、PA-2（不可变历史）、
 *     AT-08（版本锁定）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1
 *
 * 线程约束：InMemoryCatalogProvider 非线程安全（头注登记）——本组全部
 * 单线程（并发面归 project 存储端口，WP-19-T07）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>

#include "CatalogTestSupport.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

namespace {

/// 锁定对象 ID（内存形态的值语义承载——真实形态为 project 对象库 ID，
/// WP-19-T07 注入；本组用固定值）。
sdurws::ird::core::ObjectId makeLockId(const std::string& canonical)
{
    return sdurws::ird::core::ObjectId::fromCanonical(canonical);
}

}  // namespace

// =====================================================================
// 锁定与读取（AT-08 版本锁定主流程）
// =====================================================================

/**
 * 锁定→读取幂等（AT-08）：lockVersion 后 load 返回锁定时的冻结快照，
 * 重复 load 结果全等——"写入即锁定"与快照不可变的执行证明（卡 §4.2）。
 */
TEST(SelCatalogLock, LockThenLoadIsIdempotentSnapshot_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-01", "SEL-08"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());
    InMemoryCatalogProvider provider;
    const CatalogVersion lock =
        provider.lockVersion(snap, makeLockId("obj-00000000000000000000000000000001"));

    const CatalogPackageSnapshot a = provider.load(lock);
    const CatalogPackageSnapshot b = provider.load(lock);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, snap);                       // 冻结副本与锁定入参一致
    EXPECT_EQ(a.contentIdentity, snap.contentIdentity);
}

/**
 * 锁定后调用方副本可变、锁定版本不可变（PA-2）：修改入参快照后再次
 * load，锁定内容不变——历史证据不被后续编辑污染（CON-02）。
 */
TEST(SelCatalogLock, LockedSnapshotImmutableAgainstCallerMutation)
{
    IRD_TEST_INFO(std::vector<std::string>{"PA-2", "CON-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                    makeBaselineManifest());
    InMemoryCatalogProvider provider;
    const CatalogVersion lock =
        provider.lockVersion(snap, makeLockId("obj-00000000000000000000000000000002"));

    snap.motors[0].ratedTorque = 999.0;       // N·m——调用方修改自身副本
    const CatalogPackageSnapshot reloaded = provider.load(lock);
    EXPECT_DOUBLE_EQ(reloaded.motors[0].ratedTorque, 4.5);   // 锁定值不变
    EXPECT_EQ(reloaded.contentIdentity, lock.identity.contentIdentity);
}

/**
 * 同 (catalogId, version) 重复锁定拒绝（写入即锁定只增——PA-2；目录
 * 内容变更必须以新版本号声明，卡 §4.2）。
 */
TEST(SelCatalogLock, DuplicateLockOfSameCatalogVersionRejected_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "PA-2"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());
    InMemoryCatalogProvider provider;
    static_cast<void>(provider.lockVersion(snap,
                                           makeLockId("obj-00000000000000000000000000000003")));

    // 同版本再锁定（即使内容相同）＝拒绝。
    EXPECT_THROW(static_cast<void>(provider.lockVersion(
                     snap, makeLockId("obj-00000000000000000000000000000004"))),
                 std::invalid_argument);
}

/**
 * 同 (catalogId, version) 不同内容同样拒绝（只增纪律拦下"静默改版"）；
 * 内容变更走新版本号——v2 锁定成功且内容身份不同（CON-05 内容寻址）。
 */
TEST(SelCatalogLock, ContentChangeRequiresNewVersion_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-05"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogPackageSnapshot v1 = importer.assemble(makeBaselineInput(),
                                                        makeBaselineManifest("cat-demo", "v1"));

    // 同版本不同内容：额定转矩 4.5→4.6 N·m（目录"静默改版"被锁定面拒绝）。
    ParsedCatalogInput mutated = makeBaselineInput();
    ASSERT_TRUE(editRow(*mutated.files.begin(), "model_id", "M-100", "rated_torque_nm",
                        "4.6"));
    const CatalogPackageSnapshot v1Mutated = importer.assemble(mutated,
                                                               makeBaselineManifest("cat-demo", "v1"));
    EXPECT_NE(v1.contentIdentity, v1Mutated.contentIdentity);

    InMemoryCatalogProvider provider;
    static_cast<void>(provider.lockVersion(v1,
                                           makeLockId("obj-00000000000000000000000000000005")));
    EXPECT_THROW(static_cast<void>(provider.lockVersion(
                     v1Mutated,
                     makeLockId("obj-00000000000000000000000000000006"))),
                 std::invalid_argument);

    // 新版本号承载变更内容：v2 锁定成功。
    const CatalogPackageSnapshot v2 = importer.assemble(mutated,
                                                        makeBaselineManifest("cat-demo", "v2"));
    const CatalogVersion lock2 =
        provider.lockVersion(v2, makeLockId("obj-00000000000000000000000000000007"));
    EXPECT_EQ(lock2.identity.version, "v2");
    EXPECT_EQ(provider.listLocked().size(), 2U);   // 只增：v1 与 v2 并存
}

// =====================================================================
// 引用完整性（§14.1 fail-fast 面）
// =====================================================================

/** 锁定对象不存在：load fail-fast（引用完整性破坏）。 */
TEST(SelCatalogLock, LoadUnknownLockObjectRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08"},
                  std::vector<std::string>{});

    InMemoryCatalogProvider provider;
    CatalogVersion ghost;
    ghost.identity = CatalogIdentity{"cat-demo", "v1", {}, "demo"};
    ghost.lockObjectId = makeLockId("obj-000000000000000000000000000000ff");
    EXPECT_THROW(static_cast<void>(provider.load(ghost)), std::invalid_argument);
}

/** 身份三元组不符（内容摘要被篡改）：load fail-fast（卡 §14.1/§4.3）。 */
TEST(SelCatalogLock, LoadWithMismatchedContentIdentityRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogPackageSnapshot snap = importer.assemble(makeBaselineInput(),
                                                          makeBaselineManifest());
    InMemoryCatalogProvider provider;
    static_cast<void>(provider.lockVersion(snap,
                                           makeLockId("obj-00000000000000000000000000000008")));

    CatalogVersion tampered;
    tampered.identity = snap.manifest.identity;
    tampered.identity.contentIdentity.bytes[0] ^= 0xFF;   // 摘要篡改
    tampered.lockObjectId = makeLockId("obj-00000000000000000000000000000008");
    EXPECT_THROW(static_cast<void>(provider.load(tampered)), std::invalid_argument);
}

/** 无身份快照（内容身份全零）拒绝锁定（CON-05 前提——身份不可缺席）。 */
TEST(SelCatalogLock, LockingIdentitylessSnapshotRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{});

    CatalogPackageSnapshot snap;   // 默认构造＝身份全零
    snap.manifest = makeBaselineManifest();
    InMemoryCatalogProvider provider;
    EXPECT_THROW(static_cast<void>(provider.lockVersion(
                     snap, makeLockId("obj-00000000000000000000000000000009"))),
                 std::invalid_argument);
}

// =====================================================================
// 历史隔离与导入失败不替换当前目录（V1 注入项）
// =====================================================================

/** 锁定清单升序输出（确定性——NFR-COR02）＋历史版本并存。 */
TEST(SelCatalogLock, ListLockedSortedAscending)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    const CatalogImporter importer;
    InMemoryCatalogProvider provider;
    // 乱序锁定：v2 → v1 → cat-b/v1。
    static_cast<void>(provider.lockVersion(
        importer.assemble(makeBaselineInput(), makeBaselineManifest("cat-demo", "v2")),
        makeLockId("obj-0000000000000000000000000000000a")));
    static_cast<void>(provider.lockVersion(
        importer.assemble(makeBaselineInput(), makeBaselineManifest("cat-demo", "v1")),
        makeLockId("obj-0000000000000000000000000000000b")));
    static_cast<void>(provider.lockVersion(
        importer.assemble(makeBaselineInput(), makeBaselineManifest("cat-b", "v1")),
        makeLockId("obj-0000000000000000000000000000000c")));

    const std::vector<CatalogVersion> locked = provider.listLocked();
    ASSERT_EQ(locked.size(), 3U);
    EXPECT_EQ(locked[0].identity.catalogId, "cat-b");     // catalogId 升序
    EXPECT_EQ(locked[1].identity.catalogId, "cat-demo");
    EXPECT_EQ(locked[1].identity.version, "v1");          // 同目录 version 升序
    EXPECT_EQ(locked[2].identity.version, "v2");
}

/**
 * 导入失败不替换当前有效目录（V1 注入项——卡 §5.3 注"导入失败不替换
 * 当前有效目录"）：校验失败的包不产生锁定，已锁定版本原样可读。
 */
TEST(SelCatalogLock, FailedImportKeepsCurrentLockedCatalogs)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    InMemoryCatalogProvider provider;
    // 当前有效目录 v1 已锁定。
    const CatalogVersion live =
        provider.lockVersion(importer.assemble(makeBaselineInput(),
                                               makeBaselineManifest("cat-demo", "v1")),
                             makeLockId("obj-0000000000000000000000000000000d"));

    // 坏包（峰值<额定）导入失败：报告非空→不锁定（调用方纪律的执行面）。
    ParsedCatalogInput bad = makeBaselineInput();
    ASSERT_TRUE(editRow(*bad.files.begin(), "model_id", "M-100", "peak_torque_nm", "2"));
    const CatalogValidationReport rep = importer.validate(bad,
                                                          makeBaselineManifest("cat-demo", "v2"));
    ASSERT_FALSE(rep.ok());
    EXPECT_THROW(static_cast<void>(importer.assemble(bad,
                                                     makeBaselineManifest("cat-demo", "v2"))),
                 std::invalid_argument);

    // 当前目录不受影响：清单数量不变、v1 内容原样可读。
    EXPECT_EQ(provider.listLocked().size(), 1U);
    const CatalogPackageSnapshot reloaded = provider.load(live);
    EXPECT_DOUBLE_EQ(reloaded.motors[0].peakTorque, 11.0);   // N·m（锁定值不变）
}

/**
 * 目录更新不静默改变历史结果（卡 §4.2）：v1 锁定后导入 v2，v1 的读取
 * 面继续返回 v1 内容——历史结果按其原目录版本独立判定（CON-02）。
 */
TEST(SelCatalogLock, NewerVersionDoesNotAlterLockedHistory)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-08", "CON-02"},
                  std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    InMemoryCatalogProvider provider;
    const CatalogVersion v1 =
        provider.lockVersion(importer.assemble(makeBaselineInput(),
                                               makeBaselineManifest("cat-demo", "v1")),
                             makeLockId("obj-0000000000000000000000000000000e"));

    // v2：M-100 额定转矩 4.5→6.0 N·m。
    ParsedCatalogInput v2Input = makeBaselineInput();
    ASSERT_TRUE(editRow(*v2Input.files.begin(), "model_id", "M-100", "rated_torque_nm",
                        "6.0"));
    static_cast<void>(provider.lockVersion(
        importer.assemble(v2Input, makeBaselineManifest("cat-demo", "v2")),
        makeLockId("obj-0000000000000000000000000000000f")));

    // 历史引用 v1：读到旧值（目录更新不回写历史）。
    const CatalogPackageSnapshot historical = provider.load(v1);
    EXPECT_DOUBLE_EQ(historical.motors[0].ratedTorque, 4.5);   // N·m（v1 值）
    EXPECT_EQ(historical.manifest.identity.version, "v1");
}
