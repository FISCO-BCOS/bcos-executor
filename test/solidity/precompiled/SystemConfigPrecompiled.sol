pragma solidity ^0.4.24;

contract SystemConfigPrecompiled 
{
    function setValueByKey(string key, string value) public returns(int256);
    function getValueByKey(string key) public view returns(string,int256);
}
