#pragma once

#include "../Common.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include <boost/coroutine2/coroutine.hpp>

namespace bcos::executor
{
using GetStorageMessage = std::tuple<Error::UniquePtr, std::optional<storage::Entry>>;
using SetStorageMessage = std::tuple<Error::UniquePtr>;
using CreateTableMessage = std::tuple<Error::UniquePtr, std::optional<bcos::storage::Table>>;

template <class T>
class CoroutineStorageWrapper
{
public:
    CoroutineStorageWrapper(storage::StorageInterface::Ptr storage,
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
        m_storage->asyncGetPrimaryKeys(table, _condition, [this](auto&& error, auto&& keys) {
            m_push({std::move(error), std::move(keys)});
        });

        m_pull();
        auto [error, keys] = m_pull.get();

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return keys;
    }

    std::optional<storage::Entry> getRow(
        const std::string_view& table, const std::string_view& _key)
    {
        m_storage->asyncGetRow(table, _key, [this](auto&& error, auto&& entry) {
            m_push({std::move(error), std::move(entry)});
        });

        m_pull();
        auto [error, entry] = m_pull.get();

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return entry;
    }

    std::vector<std::optional<storage::Entry>> getRows(
        const std::string_view& table, const std::variant<const gsl::span<std::string_view const>,
                                           const gsl::span<std::string const>>& _keys)
    {
        m_storage->asyncGetRows(table, _keys, [this](auto&& error, auto&& entries) {
            m_push({std::move(error), std::move(entries)});
        });

        m_pull();
        auto [error, entries] = m_pull.get();

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return entries;
    }

    void setRow(const std::string_view& table, const std::string_view& key, storage::Entry entry)
    {
        m_storage->asyncSetRow(
            table, key, std::move(entry), [this](auto&& error) { m_push({std::move(error)}); });

        m_pull();
        auto [error] = m_pull.get();

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
    }

    std::optional<storage::Table> createTable(std::string _tableName, std::string _valueFields)
    {
        m_storage->asyncCreateTable(std::move(_tableName), std::move(_valueFields),
            [this](Error::UniquePtr&& error, auto&& table) {
                EXECUTOR_LOG(TRACE) << "Push create table result";
                m_push(std::tuple{std::move(error), std::move(table)});
            });

        EXECUTOR_LOG(TRACE) << "Switch the main coroutine";
        m_pull();
        auto [error, table] = std::get<CreateTableMessage>(m_pull.get());

        if (error)
        {
            BOOST_THROW_EXCEPTION(*(error));
        }

        return table;
    }

    std::optional<storage::Table> openTable(std::string_view tableName)
    {
        m_storage->asyncOpenTable(tableName, [this](auto&& error, auto&& table) {
            m_push({std::move(error), std::move(table)});
        });

        m_pull();
        auto [error, table] = m_pull.get();

        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return table;
    }

private:
    storage::StorageInterface::Ptr m_storage;
    typename boost::coroutines2::coroutine<T>::push_type& m_push;
    typename boost::coroutines2::coroutine<T>::pull_type& m_pull;
};
}  // namespace bcos::executor