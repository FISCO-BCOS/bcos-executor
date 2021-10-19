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

template <class T>
class CoroutineStorageWrapper
{
public:
    CoroutineStorageWrapper(storage::StateStorage::Ptr storage,
        typename boost::coroutines2::coroutine<T>::push_type& push,
        typename boost::coroutines2::coroutine<T>::pull_type& pull)
      : m_storage(std::move(storage)), m_push(push), m_pull(pull)
    {}

    CoroutineStorageWrapper(const CoroutineStorageWrapper&) = delete;
    CoroutineStorageWrapper(CoroutineStorageWrapper&&) = delete;
    CoroutineStorageWrapper& operator=(const CoroutineStorageWrapper&) = delete;
    CoroutineStorageWrapper& operator=(CoroutineStorageWrapper&&) = delete;

    std::vector<std::string> getPrimaryKeys(
        const std::string_view& table, const std::optional<storage::Condition const>& _condition)
    {
        std::optional<GetPrimaryKeysReponse> value;

        m_storage->asyncGetPrimaryKeys(
            table, _condition, [this, value = &value](auto&& error, auto&& keys) {
                if (m_push)
                {
                    *value = std::make_optional(std::tuple{std::move(error), std::move(keys)});
                }
                else
                {
                    m_push(GetPrimaryKeysReponse{std::move(error), std::move(keys)});
                }
            });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<GetPrimaryKeysReponse>(m_pull.get()));
        }

        auto& [error, keys] = *value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return std::move(keys);
    }

    std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key)
    {
        std::optional<GetRowResponse> value;

        m_storage->asyncGetRow(table, _key, [this, value = &value](auto&& error, auto&& entry) {
            if (m_push)
            {
                *value = std::make_optional(GetRowResponse{std::move(error), std::move(entry)});
            }
            else
            {
                m_push(GetRowResponse{std::move(error), std::move(entry)});
            }
        });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<GetRowResponse>(m_pull.get()));
        }

        auto& [error, entry] = *value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        acquireKeyLock(_key);
        return std::move(entry);
    }

    std::vector<std::optional<storage::Entry>> getRows(
        const std::string_view& table, const std::variant<const gsl::span<std::string_view const>,
                                           const gsl::span<std::string const>>& _keys)
    {
        std::optional<GetRowsResponse> value;

        m_storage->asyncGetRows(table, _keys, [this, value = &value](auto&& error, auto&& entries) {
            if (m_push)
            {
                *value = std::make_optional(GetRowsResponse{std::move(error), std::move(entries)});
            }
            else
            {
                m_push(GetRowsResponse{std::move(error), std::move(entries)});
            }
        });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<GetRowsResponse>(m_pull.get()));
        }

        auto& [error, entries] = *value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        std::visit(
            [this](auto&& keys) {
                for (auto& it : keys)
                {
                    acquireKeyLock(it);
                }
            },
            _keys);

        return std::move(entries);
    }

    void setRow(const std::string_view& table, const std::string_view& key, storage::Entry entry)
    {
        std::optional<SetRowResponse> value;

        m_storage->asyncSetRow(table, key, std::move(entry), [this, value = &value](auto&& error) {
            if (m_push)
            {
                *value = std::make_optional(SetRowResponse{std::move(error)});
            }
            else
            {
                m_push(SetRowResponse{std::move(error)});
            }
        });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<SetRowResponse>(m_pull.get()));
        }

        auto& [error] = *value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        acquireKeyLock(key);
    }

    std::optional<storage::Table> createTable(std::string _tableName, std::string _valueFields)
    {
        std::optional<OpenTableResponse> value;

        m_storage->asyncCreateTable(std::move(_tableName), std::move(_valueFields),
            [this, value = &value](Error::UniquePtr&& error, auto&& table) {
                if (m_push)
                {
                    *value =
                        std::make_optional(OpenTableResponse{std::move(error), std::move(table)});
                }
                else
                {
                    m_push(OpenTableResponse{std::move(error), std::move(table)});
                }
            });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<OpenTableResponse>(m_pull.get()));
        }

        auto& [error, table] = *value;

        if (error)
        {
            BOOST_THROW_EXCEPTION(*(error));
        }

        return std::move(table);
    }

    std::optional<storage::Table> openTable(std::string_view tableName)
    {
        std::optional<OpenTableResponse> value;

        m_storage->asyncOpenTable(tableName, [this, value = &value](auto&& error, auto&& table) {
            if (m_push)
            {
                *value = std::make_optional(OpenTableResponse{std::move(error), std::move(table)});
            }
            else
            {
                m_push(OpenTableResponse{std::move(error), std::move(table)});
            }
        });

        if (!value)
        {
            m_pull();
            value = std::make_optional(std::get<OpenTableResponse>(m_pull.get()));
        }
        auto& [error, table] = *value;

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

    void setExistsKeyLocks(gsl::span<std::string> keyLocks)
    {
        m_existsKeyLocks.reserve(keyLocks.size());
    }

private:
    void acquireKeyLock(const std::string_view key)
    {
        if (m_existsKeyLocks.contains(key))
        {
            // Wait for lock release
        }

        auto it = m_myKeyLocks.lower_bound(key);
        if (it == m_myKeyLocks.end() || *it != key)
        {
            m_myKeyLocks.emplace_hint(it, key);
        }
    }

    storage::StateStorage::Ptr m_storage;
    typename boost::coroutines2::coroutine<T>::push_type& m_push;
    typename boost::coroutines2::coroutine<T>::pull_type& m_pull;

    boost::container::flat_set<std::string, std::less<>> m_existsKeyLocks;
    std::set<std::string, std::less<>> m_myKeyLocks;
};
}  // namespace bcos::executor