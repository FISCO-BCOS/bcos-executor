pragma solidity ^0.4.24;

contract ContractAuthPrecompiled {
    function getAdmin(string path) public view returns (address);
    function resetAdmin(string path, address admin) public returns (int256);
    function setMethodAuthType(string path, bytes4 func, uint8 authType) public returns (int256);
    function openMethodAuth(string path, bytes4 func, address account) public returns (int256);
    function closeMethodAuth(string path, bytes4 func, address account) public returns (int256);
    function checkMethodAuth(string path, bytes4 func, address account) public returns (bool);
}
