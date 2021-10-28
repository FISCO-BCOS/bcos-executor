pragma solidity ^0.4.25;

import "./precompiled/CRUDPrecompiled.sol";
import "./precompiled/Table.sol";
import "../../src/precompiled/solidity/Table.sol";


contract TestCRUD {
    CRUDPrecompiled crud;
    constructor() public {
        crud = CRUDPrecompiled(0x1002);
    }
    function insert(string tableName, string entry, string option) public returns (int256){
        return crud.insert(tableName, entry, option);
    }

    function remove(string tableName, string condition, string option) public returns (int256){
        return crud.remove(tableName, condition, option);
    }

    function select(string tableName, string condition, string option) public view returns (string){
        return crud.select(tableName, condition, option);
    }

    function update(string tableName, string entry, string condition, string option) public returns (int256){
        return crud.update(tableName, entry, condition, option);
    }

    function desc(string tableName) public view returns (string, string){
        return crud.desc(tableName);
    }
}

contract TestTable {
    TableFactory tableFactory;
    KVTableFactory kvTableFactory;
    constructor() public {
        tableFactory = TableFactory(0x1001);
        kvTableFactory = KVTableFactory(0x1009);
    }

    function openTable(string name) public view returns (Table){
        return tableFactory.openTable(name);
    }

    function createTable(string tableName, string keyField, string valueFields) public returns (int256){
        return tableFactory.createTable(tableName, keyField, valueFields);
    }

    function openKVTable(string name) public view returns (KVTable){
        return kvTableFactory.openTable(name);
    }

    function createKVTable(string tableName, string keyField, string valueFields) public returns (int256){
        return kvTableFactory.createTable(tableName, keyField, valueFields);
    }

    function select(string tableName, Condition cond) public view returns (Entries){
        Table table = tableFactory.openTable(tableName);
        return table.select(cond);
    }

    function insert(string tableName, Entry entry) public returns (int256){
        Table table = tableFactory.openTable(tableName);
        return table.insert(entry);
    }

    function update(string tableName, Entry entry, Condition cond) public returns (int256){
        Table table = tableFactory.openTable(tableName);
        return table.update(entry, cond);
    }

    function remove(string tableName, Condition cond) public returns (int256){
        Table table = tableFactory.openTable(tableName);
        return table.remove(cond);
    }

    function newEntry(string tableName) public view returns (Entry){
        Table table = tableFactory.openTable(tableName);
        return table.newEntry();
    }

    function newCondition(string tableName) public view returns (Condition){
        Table table = tableFactory.openTable(tableName);
        return table.newCondition();
    }

    function get(string tableName, string key) public view returns (bool, Entry){
        KVTable kvTable = kvTableFactory.openTable(tableName);
        return table.get(key);
    }

    function set(string tableName, string key, Entry entry) public returns (int256){
        KVTable kvTable = kvTableFactory.openTable(tableName);
        return table.set(key, entry);
    }

    function newKVEntry(string tableName) public view returns (Entry){
        KVTable kvTable = kvTableFactory.openTable(tableName);
        return table.newEntry();
    }
}
