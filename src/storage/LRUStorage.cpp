#include "LRUStorage.h"
#include "../Common.h"
#include <boost/iterator/zip_iterator.hpp>
#include <algorithm>
#include <thread>

using namespace bcos::executor;

void LRUStorage::asyncGetPrimaryKeys(const std::string_view& table,
    const std::optional<bcos::storage::Condition const>& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback)
{
    storage::StateStorage::asyncGetPrimaryKeys(table, _condition, std::move(_callback));
}

void LRUStorage::asyncGetRow(const std::string_view& table, const std::string_view& _key,
    std::function<void(Error::UniquePtr, std::optional<bcos::storage::Entry>)> _callback)
{
    storage::StateStorage::asyncGetRow(table, _key,
        [this, callback = std::move(_callback), table, key = _key](
            Error::UniquePtr error, std::optional<bcos::storage::Entry> entry) {
            if (!error)
            {
                updateMRU(EntryKeyWrapper(table, std::string(key), entry->capacityOfHashField()));
            }
            callback(std::move(error), std::move(entry));
        });
}

void LRUStorage::asyncGetRows(const std::string_view& table,
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<bcos::storage::Entry>>)>
        _callback)
{
    std::vector<std::string> keys;

    std::visit(
        [&keys](auto&& input) {
            keys.reserve(input.size());
            for (auto& it : input)
            {
                keys.emplace_back(it);
            }
        },
        _keys);

    storage::StateStorage::asyncGetRows(table, _keys,
        [this, table, keys = std::move(keys), callback = std::move(_callback)](
            Error::UniquePtr error, std::vector<std::optional<bcos::storage::Entry>> entries) {
            if (!error && keys.size() == entries.size())
            {
                for (size_t i = 0; i < keys.size(); ++i)
                {
                    auto& key = keys[i];
                    auto& entry = entries[i];

                    if (entry)
                    {
                        updateMRU(EntryKeyWrapper(table, key, entry->capacityOfHashField()));
                    }
                }
            }

            callback(std::move(error), std::move(entries));
        });
}

void LRUStorage::asyncSetRow(const std::string_view& table, const std::string_view& key,
    bcos::storage::Entry entry, std::function<void(Error::UniquePtr)> callback)
{
    updateMRU(EntryKeyWrapper(table, std::string(key), entry.capacityOfHashField()));
    storage::StateStorage::asyncSetRow(table, key, std::move(entry), std::move(callback));
}

void LRUStorage::merge(bool onlyDirty, const TraverseStorageInterface& source)
{
    source.parallelTraverse(
        onlyDirty, [this](const std::string_view& table, const std::string_view& key,
                       const storage::Entry& entry) {
            asyncSetRow(table, key, entry, [](Error::UniquePtr) {});
            return true;
        });
}

void LRUStorage::start()
{
    m_running = true;
    m_worker = std::make_unique<std::thread>([self = shared_from_this()]() { self->startLoop(); });
}

void LRUStorage::stop()
{
    if (m_running)
    {
        m_running = false;

        m_mruQueue.emplace(EntryKeyWrapper());
        m_worker->join();
    }
}

void LRUStorage::startLoop()
{
    while (true)
    {
        EntryKeyWrapper entryKey;
        if (m_mruQueue.try_pop(entryKey))
        {
            EXECUTOR_LOG(TRACE) << "Pop key: " << entryKey.table() << " " << entryKey.key() << " " << entryKey.capacity;
            // Check if stopped
            if (entryKey.isStop())
            {
                break;
            }

            // Push item to mru
            auto result = m_mru.emplace_back(std::move(entryKey));
            if (result.second)
            {
                m_capacity += entryKey.capacity;
            }
            else
            {
                m_mru.relocate(m_mru.end(), result.first);
            }

            // Check if out of capacity
            EXECUTOR_LOG(TRACE) << "capacity: " << m_capacity << " max_capacity: " << m_maxCapacity;
            if (m_capacity > m_maxCapacity)
            {
                // Clear the out date items
                while (m_capacity > m_maxCapacity)
                {
                    auto& item = m_mru.front();

                    bcos::storage::Entry entry;
                    entry.setStatus(bcos::storage::Entry::DELETED);

                    storage::StateStorage::asyncSetRow(
                        item.table(), item.key(), std::move(entry), [](Error::UniquePtr) {});

                    m_mru.pop_front();
                }
            }
        }
        else
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(200ms);
        }
    }
}

void LRUStorage::updateMRU(EntryKeyWrapper entryKey)
{
    m_mruQueue.push(std::move(entryKey));
}