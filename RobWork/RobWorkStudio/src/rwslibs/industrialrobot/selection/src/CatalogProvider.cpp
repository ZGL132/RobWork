/**
 * @file   CatalogProvider.cpp
 * @brief  目录快照锁定供给与 P-IO-7 注册面实现（selection 单元）——
 *         InMemoryCatalogProvider（写入即锁定只增/锁定后不可变/摘要不符
 *         拒绝）＋catalogPackageFileSchema（v1 文件清单注册数据）。
 *
 * 设计依据：
 *   - units/selection.md §4.2（目录数据冻结——写入即锁定、只增；不完整
 *     导入＝删除重导；文件路径不作为目录身份；内容身份变更→切片失效）、
 *     §4.3（身份关系表——(catalogId, version, 包内容摘要) 三元组）、
 *     §5.2（P-IO-7 注册义务——目录包文件清单/文件名结构契约由本卡注册，
 *     io 卡 §7.8 校验执行框架按本表执行）、§14.1（ICatalogProvider 契约
 *     ——load 摘要不符 fail-fast）
 *   - 需求 SEL-01（版本化目录包）、SEL-08（项目锁定版本——本文件的锁定
 *     语义是其本域执行面，project 端口形态随 WP-19-T07）、AT-08（版本
 *     锁定）、PA-2（不可变历史——锁定表只增不删）、CON-05（内容寻址）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1（"版本锁定"
 *     测试面）/3（P-IO-7 注册面与分工）
 *
 * 线程安全：InMemoryCatalogProvider 非线程安全（锁定表可变——仅装配/
 * 测试单线程使用；真实并发面归 project 存储端口，WP-19-T07）。
 */

#include <sdurws/ird/selection/CatalogProvider.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sdurws::ird::selection {

// =====================================================================
// InMemoryCatalogProvider（版本锁定语义载体——AT-08 判定面）
// =====================================================================

CatalogVersion InMemoryCatalogProvider::lockVersion(const CatalogPackageSnapshot& snapshot,
                                                    core::ObjectId lockObjectId)
{
    // ① 快照身份有效性：包内容身份全零＝无身份快照（CON-05 内容寻址前提
    // 破坏——锁定一个无身份的快照会使引用完整性无从校验）。
    if (!snapshot.contentIdentity.isValid()) {
        throw std::invalid_argument(
            "SEL-CATALOG(lock): 快照包内容身份无效（全零——须先经 "
            "CatalogImporter::assemble 计算内容身份，CON-05）");
    }

    // ② 只增纪律：同一 (catalogId, version) 已锁定即拒绝（PA-2 不可变
    // 历史——锁定表只增不删；重复锁定会制造两个 lockObjectId 指向同
    // 版本的歧义引用）。注意：不同内容的同 (catalogId, version) 也被
    // 此纪律拦截——目录内容变更必须以新 version 声明（卡 §4.2"目录
    // 更新不静默改变历史结果"的锁定面）。
    for (const LockedEntry& e : locked_) {
        if (e.reference.identity.catalogId == snapshot.manifest.identity.catalogId
            && e.reference.identity.version == snapshot.manifest.identity.version) {
            throw std::invalid_argument(
                "SEL-CATALOG(lock): 版本已锁定（" + snapshot.manifest.identity.catalogId
                + "/" + snapshot.manifest.identity.version
                + "——写入即锁定只增，PA-2；内容变更须以新版本号导入）");
        }
    }

    // ③ 追加锁定记录（快照拷贝冻结——之后调用方对入参快照的任何修改
    // 不影响锁定副本；锁定序＝追加序，遍历输出时按身份升序重排）。
    LockedEntry entry;
    entry.reference.identity = snapshot.manifest.identity;
    entry.reference.lockObjectId = lockObjectId;
    entry.snapshot = snapshot;
    locked_.push_back(std::move(entry));
    return locked_.back().reference;
}

CatalogPackageSnapshot InMemoryCatalogProvider::load(const CatalogVersion& lock) const
{
    // 引用完整性（卡 §14.1 @throws——锁定对象不存在/内容摘要不符均
    // fail-fast：以不符身份的数据继续计算会破坏证据链，拒绝即防御）。
    for (const LockedEntry& e : locked_) {
        if (!(e.reference.lockObjectId == lock.lockObjectId)) { continue; }
        // lockObjectId 命中后校验身份三元组一致性（catalogId/version/
        // contentIdentity——卡 §4.3；任一不符＝引用与锁定记录脱钩）。
        if (e.reference.identity == lock.identity) {
            return e.snapshot;   // 冻结副本拷贝（调用方持有；快照不可变纪律）
        }
        throw std::invalid_argument(
            "SEL-CATALOG(lock): 目录身份与锁定记录不符（catalogId/version/"
            "contentIdentity 三元组不一致——引用完整性破坏，卡 §14.1/§4.3）");
    }
    throw std::invalid_argument(
        "SEL-CATALOG(lock): 锁定对象不存在（lockObjectId 无记录——引用完整性破坏）");
}

std::vector<CatalogVersion> InMemoryCatalogProvider::listLocked() const
{
    std::vector<CatalogVersion> out;
    out.reserve(locked_.size());
    for (const LockedEntry& e : locked_) {
        out.push_back(e.reference);
    }
    // 升序输出（catalogId → version——确定性序，NFR-COR-02；锁定表本身
    // 保持追加序＝只增历史序，PA-2）。
    std::sort(out.begin(), out.end(),
              [](const CatalogVersion& a, const CatalogVersion& b) {
                  if (a.identity.catalogId != b.identity.catalogId) {
                      return a.identity.catalogId < b.identity.catalogId;
                  }
                  return a.identity.version < b.identity.version;
              });
    return out;
}

// =====================================================================
// P-IO-7 注册面（v1 文件清单 schema——卡 §5.2 表的物化）
// =====================================================================

std::vector<ManifestEntry> catalogPackageFileSchema()
{
    // 序＝卡 §5.2 表行序（manifest → motors → gearboxes → curves →
    // compatibility——确定性序，NFR-COR-02；五文件全部必备，曲线表/兼容
    // 表允许零数据行但文件必须在包内——卡 §5.2 必备性列）。role 词表值
    // ＝本域冻结（io 清单核对框架按 role/required 执行文件层核对——
    // io 卡 §7.8；sha256 由 io 在核对期填装，注册形态留空承载位）。
    return {
        {kCatalogFileManifest,      "manifest",      true, ""},
        {kCatalogFileMotors,        "motors",        true, ""},
        {kCatalogFileGearboxes,     "gearboxes",     true, ""},
        {kCatalogFileCurves,        "curves",        true, ""},
        {kCatalogFileCompatibility, "compatibility", true, ""},
    };
}

}  // namespace sdurws::ird::selection
