#pragma once

#include "../Common.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-framework/libstorage/StateStorage.h"
#include <boost/container/flat_set.hpp>
#include <boost/coroutine2/coroutine.hpp>
#include <boost/coroutine2/fixedsize_stack.hpp>
#include <boost/iterator/iterator_categories.hpp>
#include <optional>
#include <thread>
#include <vector>

namespace bcos::executor
{
using GetPrimaryKeysReponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;
using GetRowResponse = std::tuple<Error::UniquePtr, std::optional<storage::Entry>>;
using GetRowsResponse = std::tuple<Error::UniquePtr, std::vector<std::optional<storage::Entry>>>;
using SetRowResponse = std::tuple<Error::UniquePtr>;
using OpenTableResponse = std::tuple<Error::UniquePtr, std::optional<storage::Table>>;
using KeyLockResponse = SetRowResponse;
using AcquireKeyLockResponse = std::tuple<Error::UniquePtr, std::vector<std::string>>;

template <class Resume>
class CoroutineStorageWrapper
{
public:
    CoroutineStorageWrapper(storage::StateStorage::Ptr storage,
        std::function<void(std::function<void(Resume)>)> spawnAndCall,
        std::function<void(std::string)> externalAcquireKeyLocks,
        bcos::storage::StateStorage::Recoder::Ptr recoder)
      : m_storage(std::move(storage)),
        m_spawnAndCall(std::move(spawnAndCall)),
        m_externalAcquireKeyLocks(std::move(externalAcquireKeyLocks)),
        m_recoder(recoder)
    {}

    CoroutineStorageWrapper(const CoroutineStorageWrapper&) = delete;
    CoroutineStorageWrapper(CoroutineStorageWrapper&&) = delete;
    CoroutineStorageWrapper& operator=(const CoroutineStorageWrapper&) = delete;
    CoroutineStorageWrapper& operator=(CoroutineStorageWrapper&&) = delete;

    std::vector<std::string> getPrimaryKeys(
        const std::string_view& table, const std::optional<storage::Condition const>& _condition)
    {
        GetPrimaryKeysReponse value;
        m_spawnAndCall([this, &table, &_condition, &value](Resume resume) mutable {
            m_storage->asyncGetPrimaryKeys(
                table, _condition, [this, &value, &resume](auto&& error, auto&& keys) mutable {
                    value = {std::move(error), std::move(keys)};
                    resume();
                });
        });

        // After coroutine switch, set the recoder
        setRecoder(m_recoder);

        auto& [error, keys] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(keys);
    }

    std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key)
    {
        acquireKeyLock(_key);

        GetRowResponse value;
        m_spawnAndCall([this, &table, &_key, &value](Resume resume) mutable {
            m_storage->asyncGetRow(
                table, _key, [this, &value, &resume](auto&& error, auto&& entry) mutable {
                    value = std::tuple{std::move(error), std::move(entry)};
                    resume();
                });
        });

        auto& [error, entry] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(entry);
    }

    std::vector<std::optional<storage::Entry>> getRows(
        const std::string_view& table, const std::variant<const gsl::span<std::string_view const>,
                                           const gsl::span<std::string const>>& _keys)
    {
        std::visit(
            [this](auto&& keys) {
                for (auto& it : keys)
                {
                    acquireKeyLock(it);
                }
            },
            _keys);

        GetRowsResponse value;
        m_spawnAndCall([this, &table, &_keys, &value](Resume resume) mutable {
            m_storage->asyncGetRows(
                table, _keys, [this, &value, &resume](auto&& error, auto&& entries) mutable {
                    value = std::tuple{std::move(error), std::move(entries)};
                    resume();
                });
        });


        auto& [error, entries] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(entries);
    }

    void setRow(const std::string_view& table, const std::string_view& key, storage::Entry entry)
    {
        acquireKeyLock(key);

        SetRowResponse value;

        m_spawnAndCall([this, &table, &key, &value, &entry](Resume resume) mutable {
            m_storage->asyncSetRow(
                table, key, std::move(entry), [this, &value, &resume](auto&& error) mutable {
                    value = std::tuple{std::move(error)};
                    resume();
                });
        });

        auto& [error] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
    }

    std::optional<storage::Table> createTable(std::string _tableName, std::string _valueFields)
    {
        OpenTableResponse value;

        m_spawnAndCall([this, &_tableName, &_valueFields, &value](Resume resume) mutable {
            m_storage->asyncCreateTable(std::move(_tableName), std::move(_valueFields),
                [this, &value, &resume](Error::UniquePtr&& error, auto&& table) mutable {
                    value = std::tuple{std::move(error), std::move(table)};
                    resume();
                });
        });


        auto& [error, table] = value;

        return std::move(table);
    }

    std::optional<storage::Table> openTable(std::string_view tableName)
    {
        OpenTableResponse value;

        m_spawnAndCall([this, &tableName, &value](Resume resume) mutable {
            m_storage->asyncOpenTable(
                tableName, [this, &value, &resume](auto&& error, auto&& table) mutable {
                    value = std::tuple{std::move(error), std::move(table)};
                    resume();
                });
        });


        auto& [error, table] = value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(table);
    }

    void setRecoder(storage::StateStorage::Recoder::Ptr recoder)
    {
        m_storage->setRecoder(std::move(recoder));
    }

    void importExistsKeyLocks(gsl::span<std::string> keyLocks)
    {
        m_existsKeyLocks.clear();

        for (auto& it : keyLocks)
        {
            m_existsKeyLocks.emplace(std::move(it));
        }
    }

    std::vector<std::string> exportKeyLocks()
    {
        std::vector<std::string> keyLocks;
        keyLocks.reserve(m_myKeyLocks.size());
        for (auto& it : m_myKeyLocks)
        {
            keyLocks.emplace_back(std::move(it));
        }

        m_myKeyLocks.clear();

        return keyLocks;
    }

private:
    void acquireKeyLock(const std::string_view& key)
    {
        if (m_existsKeyLocks.find(key) != m_existsKeyLocks.end())
        {
            m_externalAcquireKeyLocks(std::string(key));
        }

        auto it = m_myKeyLocks.lower_bound(key);
        if (it == m_myKeyLocks.end() || *it != key)
        {
            m_myKeyLocks.emplace_hint(it, key);
        }
    }

    storage::StateStorage::Ptr m_storage;
    std::function<void(std::function<void(Resume)>)> m_spawnAndCall;
    std::function<void(std::string)> m_externalAcquireKeyLocks;
    bcos::storage::StateStorage::Recoder::Ptr m_recoder;

    std::set<std::string, std::less<>> m_existsKeyLocks;
    std::set<std::string, std::less<>> m_myKeyLocks;
};
}  // namespace bcos::executor