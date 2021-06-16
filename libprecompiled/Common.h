/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file Common.h
 * @author: kyonRay
 * @date 2021-05-25
 */

#pragma once

#include <bcos-framework/libutilities/Log.h>
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/storage/Common.h>
#include <memory>
#include <string>

namespace bcos {
namespace precompiled {
#define PRECOMPILED_LOG(LEVEL) LOG(LEVEL) << "[PRECOMPILED]"

using EntriesConstPtr = std::shared_ptr<const std::vector<storage::Entry::Ptr>>;
using Entries = std::vector<storage::Entry::Ptr>;
using EntriesPtr = std::shared_ptr<std::vector<storage::Entry::Ptr>>;

/// SYS_ACCESS_TABLE table
const char* const SYS_ACCESS_TABLE = "s_table_access";
/// SYS_ACCESS_TABLE table fields
const char* const SYS_AC_TABLE_NAME = "table_name";
const char* const SYS_AC_ADDRESS = "address";
const char* const SYS_AC_ENABLE_NUM = "enable_num";

/// SYS_CNS table
const char* const SYS_CNS = "s_cns";
const char* const SYS_TABLE = "s_tables";

/// SYS_CONFIG table fields
static const char* const SYS_CONFIG = "s_config";
static const char* const SYS_VALUE = "value";
static const char* const SYS_KEY = "key";
static const char* const SYS_CONFIG_ENABLE_BLOCK_NUMBER = "enable_block_number";

/// FileSystem table keys
static const std::string FS_KEY_TYPE = "type";
static const std::string FS_KEY_SUB = "subdirectories";
static const std::string FS_KEY_NUM = "enable_number";

/// FileSystem file type
static const std::string FS_TYPE_DIR = "directory";
static const std::string FS_TYPE_CONTRACT = "contract";
static const std::string FS_TYPE_LINK = "link";

/// SYS_CONSENSUS table fields
static const char* const NODE_TYPE = "type";
static const char* const NODE_WEIGHT = "weight";
static const char* const NODE_ENABLE_NUMBER = "enable_block_number";

const int SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH = 64;
const int SYS_TABLE_KEY_FIELD_MAX_LENGTH = 1024;
const int SYS_TABLE_VALUE_FIELD_MAX_LENGTH = 1024;
const int CNS_VERSION_MAX_LENGTH = 128;
const int CNS_CONTRACT_NAME_MAX_LENGTH = 128;
const int USER_TABLE_KEY_VALUE_MAX_LENGTH = 255;
const int USER_TABLE_FIELD_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH = 64;
const int USER_TABLE_NAME_MAX_LENGTH_S = 50;
const int USER_TABLE_FIELD_VALUE_MAX_LENGTH = 16 * 1024 * 1024 - 1;

// Define precompiled contract address
const char* const SYS_CONFIG_NAME = "/bin/status";
const char* const TABLE_FACTORY_NAME = "/bin/storage";
const char* const CRUD_NAME = "/bin/crud";
const char* const CONSENSUS_NAME = "/bin/consensus";
const char* const CNS_NAME = "/bin/cns";
const char* const PERMISSION_NAME = "/bin/permission";
const char* const PARALLEL_CONFIG_NAME = "/bin/parallel_config";
//const char* const CONTRACT_LIFECYCLE_NAME = "/sys/contract_life_cycle";
//const char* const CHAIN_GOVERNANCE_NAME = "/sys/governance";
const char* const KV_TABLE_FACTORY_NAME = "/bin/kv_storage";
//const char* const WORKING_SEALER_MGR_NAME = "/sys/sealer_manager";

// precompiled contract for solidity
const char* const SYS_CONFIG_ADDRESS = "0x1000";
const char* const TABLE_FACTORY_ADDRESS = "0x1001";
const char* const CRUD_ADDRESS = "0x1002";
const char* const CONSENSUS_ADDRESS = "0x1003";
const char* const CNS_ADDRESS = "0x1004";
const char* const PERMISSION_ADDRESS = "0x1005";
const char* const PARALLEL_CONFIG_ADDRESS = "0x1006";
const char* const CONTRACT_LIFECYCLE_ADDRESS = "0x1007";
const char* const CHAIN_GOVERNANCE_ADDRESS = "0x1008";
const char* const KV_TABLE_FACTORY_ADDRESS = "0x1009";
const char* const CRYPTO_ADDRESS = "0x100a";
const char* const WORKING_SEALER_MGR_ADDRESS = "0x100b";
const char* const DAG_TRANSFER_ADDRESS = "0x100c";

const int CODE_NO_AUTHORIZED = -50000;
const int CODE_TABLE_NAME_ALREADY_EXIST = -50001;
const int CODE_TABLE_NAME_LENGTH_OVERFLOW = -50002;
const int CODE_TABLE_FIELD_LENGTH_OVERFLOW = -50003;
const int CODE_TABLE_FIELD_TOTAL_LENGTH_OVERFLOW = -50004;
const int CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW = -50005;
const int CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW = -50006;
const int CODE_TABLE_DUPLICATE_FIELD = -50007;
const int CODE_TABLE_INVALIDATE_FIELD = -50008;

const int TX_COUNT_LIMIT_MIN = 1;
const int TX_GAS_LIMIT_MIN = 100000;
const unsigned SYSTEM_CONSENSUS_TIMEOUT_MIN = 3;
const unsigned SYSTEM_CONSENSUS_TIMEOUT_MAX = (UINT_MAX / 1000);

enum PrecompiledErrorCode : int
{
    // FileSystemPrecompiled -53099 ~ -53000
    CODE_FILE_NOT_EXIST = -53001,

    // ChainGovernancePrecompiled -52099 ~ -52000
    CODE_CURRENT_VALUE_IS_EXPECTED_VALUE = -52012,
    CODE_ACCOUNT_FROZEN = -52011,
    CODE_ACCOUNT_ALREADY_AVAILABLE = -52010,
    CODE_INVALID_ACCOUNT_ADDRESS = -52009,
    CODE_ACCOUNT_NOT_EXIST = -52008,
    CODE_OPERATOR_NOT_EXIST = -52007,
    CODE_OPERATOR_EXIST = -52006,
    CODE_COMMITTEE_MEMBER_CANNOT_BE_OPERATOR = -52005,
    CODE_OPERATOR_CANNOT_BE_COMMITTEE_MEMBER = -52004,
    CODE_INVALID_THRESHOLD = -52003,
    CODE_INVALID_REQUEST_PERMISSION_DENIED = -52002,
    CODE_COMMITTEE_MEMBER_NOT_EXIST = -52001,
    CODE_COMMITTEE_MEMBER_EXIST = -52000,

    // ContractLifeCyclePrecompiled -51999 ~ -51900
    CODE_INVALID_REVOKE_LAST_AUTHORIZATION = -51907,
    CODE_INVALID_NON_EXIST_AUTHORIZATION = -51906,
    CODE_INVALID_NO_AUTHORIZED = -51905,
    CODE_INVALID_TABLE_NOT_EXIST = -51904,
    CODE_INVALID_CONTRACT_ADDRESS = -51903,
    CODE_INVALID_CONTRACT_REPEAT_AUTHORIZATION = -51902,
    CODE_INVALID_CONTRACT_AVAILABLE = -51901,
    CODE_INVALID_CONTRACT_FROZEN = -51900,

    // RingSigPrecompiled -51899 ~ -51800
    VERIFY_RING_SIG_FAILED = -51800,

    // GroupSigPrecompiled -51799 ~ -51700
    VERIFY_GROUP_SIG_FAILED = -51700,

    // PaillierPrecompiled -51699 ~ -51600
    CODE_INVALID_CIPHERS = -51600,

    // CRUDPrecompiled -51599 ~ -51500
    CODE_INVALID_UPDATE_TABLE_KEY = -51503,
    CODE_CONDITION_OPERATION_UNDEFINED = -51502,
    CODE_PARSE_CONDITION_ERROR = -51501,
    CODE_PARSE_ENTRY_ERROR = -51500,

    // DagTransferPrecompiled -51499 ~ -51400
    CODE_INVALID_OPENTABLE_FAILED = -51406,
    CODE_INVALID_BALANCE_OVERFLOW = -51405,
    CODE_INVALID_INSUFFICIENT_BALANCE = -51404,
    CODE_INVALID_USER_ALREADY_EXIST = -51403,
    CODE_INVALID_USER_NOT_EXIST = -51402,
    CODE_INVALID_AMOUNT = -51401,
    CODE_INVALID_USER_NAME = -51400,

    // SystemConfigPrecompiled -51399 ~ -51300
    CODE_INVALID_CONFIGURATION_VALUES = -51300,

    // CNSPrecompiled -51299 ~ -51200
    CODE_VERSION_LENGTH_OVERFLOW = -51201,
    CODE_ADDRESS_AND_VERSION_EXIST = -51200,

    // ConsensusPrecompiled -51199 ~ -51100
    CODE_INVALID_NODE_ID = -51100,
    CODE_LAST_SEALER = -51101,
    CODE_INVALID_WEIGHT = -51102,
    CODE_NODE_NOT_EXIST = -51103,

    // PermissionPrecompiled -51099 ~ -51000
    CODE_COMMITTEE_PERMISSION = -51004,
    CODE_CONTRACT_NOT_EXIST = -51003,
    CODE_TABLE_NAME_OVERFLOW = -51002,
    CODE_TABLE_AND_ADDRESS_NOT_EXIST = -51001,
    CODE_TABLE_AND_ADDRESS_EXIST = -51000,

    // Common error code among all precompiled contracts -50199 ~ -50100
    CODE_ADDRESS_INVALID = -50102,
    CODE_UNKNOW_FUNCTION_CALL = -50101,
    CODE_TABLE_NOT_EXIST = -50100,

    // correct return: code great or equal 0
    CODE_SUCCESS = 0
};

enum ContractStatus
{
    Invalid = 0,
    Available,
    Frozen,
    AddressNonExistent,
    NotContractAddress,
    Count
};
} // namespace precompiled
} // namespace bcos