#pragma once

#include <bcos-framework/interfaces/storage/StorageInterface.h>
#include <bcos-framework/libstorage/StateStorage.h>
#include <tbb/concurrent_queue.h>
#include <tbb/task_group.h>
#include <tbb/tbb_thread.h>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace bcos::executor
{
class LRUStorage : public virtual bcos::storage::StateStorage,
                   public virtual bcos::storage::MergeableStorageInterface,
                   public std::enable_shared_from_this<LRUStorage>
{
public:
    LRUStorage(std::shared_ptr<StorageInterface> prev) : StateStorage(std::move(prev)) {}
    ~LRUStorage() noexcept override { stop(); }

    void asyncGetPrimaryKeys(const std::string_view& table,
        const std::optional<bcos::storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(const std::string_view& table, const std::string_view& _key,
        std::function<void(Error::UniquePtr, std::optional<bcos::storage::Entry>)> _callback)
        override;

    void asyncGetRows(const std::string_view& table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<bcos::storage::Entry>>)>
            _callback) override;

    void asyncSetRow(const std::string_view& table, const std::string_view& key,
        bcos::storage::Entry entry, std::function<void(Error::UniquePtr)> callback) override;

    void merge(bool onlyDirty, const TraverseStorageInterface& source) override;

    void start();
    void stop();

private:
    void startLoop();

    struct EntryKeyWrapper : public EntryKey
    {
        EntryKeyWrapper() : EntryKey(), capacity(0) {}
        EntryKeyWrapper(std::string_view table, std::string key, size_t _capacity)
          : EntryKey(table, std::move(key)), capacity(_capacity)
        {}

        EntryKeyWrapper(const EntryKeyWrapper&) = delete;
        EntryKeyWrapper& operator=(const EntryKeyWrapper&) = delete;

        EntryKeyWrapper(EntryKeyWrapper&&) noexcept = default;
        EntryKeyWrapper& operator=(EntryKeyWrapper&&) noexcept = default;


        std::tuple<std::string_view, std::string_view> tableKeyView() const
        {
            return {table(), key()};
        }

        bool isStop() const { return table().empty() && key().empty() && capacity == 0; }

        size_t capacity;
    };

    void updateMRU(EntryKeyWrapper entryKey);

    boost::multi_index_container<EntryKeyWrapper,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<EntryKeyWrapper,
                std::tuple<std::string_view, std::string_view>, &EntryKeyWrapper::tableKeyView>>>>
        m_mru;
    tbb::concurrent_queue<EntryKeyWrapper> m_mruQueue;

    int64_t m_maxCapacity = 256 * 1024 * 1024;  // default 256MB for cache
    int64_t m_capacity = 0;

    std::unique_ptr<std::thread> m_worker;
    std::atomic_bool m_running = false;

    tbb::atomic<uint64_t> m_hitTimes;
    tbb::atomic<uint64_t> m_queryTimes;
};
}  // namespace bcos::executor