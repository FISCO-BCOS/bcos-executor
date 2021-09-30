#pragma once

#include <tbb/concurrent_unordered_map.h>
#include <forward_list>
#include <string_view>

namespace bcos::executor
{
class KeyLocks
{
public:
    using Ptr = std::shared_ptr<KeyLocks>;

    KeyLocks() = default;
    KeyLocks(const KeyLocks&) = delete;
    KeyLocks(KeyLocks&&) = delete;
    KeyLocks& operator=(const KeyLocks&) = delete;
    KeyLocks& operator=(KeyLocks&&) = delete;

    bool acquireKeyLock(const std::string_view& table, const std::string_view& key, int contextID);

    void releaseKeyLocks(int contextID);

private:
    tbb::concurrent_unordered_map<std::tuple<std::string_view, std::string_view>, int64_t>
        m_key2ContextID;
    tbb::concurrent_unordered_map<int64_t,
        std::forward_list<std::tuple<std::string_view, std::string_view>>>
        m_contextID2Key;
};
}  // namespace bcos::executor