#ifndef RWS_STRUCTUREOPTIMIZATION_EVALUATIONCACHE_HPP
#define RWS_STRUCTUREOPTIMIZATION_EVALUATIONCACHE_HPP

#include "CacheKey.hpp"

#include <map>

namespace rws {

/** 单机内存缓存；磁盘持久化留到内存语义验证完成后再实现。 */
class EvaluationCache
{
  public:
    bool put(const CacheKeyInput& input, const CandidateResult& result);
    bool find(const CacheKeyInput& input, CandidateResult& result) const;
    void clear();
    std::size_t size() const { return _entries.size(); }
    std::size_t hitCount() const { return _hits; }

  private:
    std::map<CacheKey, CandidateResult> _entries;
    mutable std::size_t _hits = 0;
};

} // namespace rws

#endif
