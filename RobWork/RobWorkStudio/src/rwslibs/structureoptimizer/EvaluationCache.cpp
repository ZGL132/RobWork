#include "EvaluationCache.hpp"

namespace rws {

bool EvaluationCache::put(const CacheKeyInput& input, const CandidateResult& result)
{
    const CacheKeyResult key = CacheKey::create(input);
    if (!key.ok) return false;
    _entries[key.key] = result;
    return true;
}

bool EvaluationCache::find(const CacheKeyInput& input, CandidateResult& result) const
{
    const CacheKeyResult key = CacheKey::create(input);
    if (!key.ok) return false;
    const auto found = _entries.find(key.key);
    if (found == _entries.end()) return false;
    result = found->second;
    ++_hits;
    return true;
}

void EvaluationCache::clear()
{
    _entries.clear();
    _hits = 0;
}

} // namespace rws
