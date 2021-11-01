#pragma once

#include <bcos-framework/interfaces/storage/StorageInterface.h>

namespace bcos::executor
{
class LRUStorage : public bcos::storage::MergeableStorageInterface
{
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
};
}  // namespace bcos::executor