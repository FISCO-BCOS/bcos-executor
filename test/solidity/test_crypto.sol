pragma solidity ^0.4.25;

import "./precompiled/Crypto.sol";

contract TestCrypto {
    Crypto crypto;
    constructor () public {
        crypto = Crypto(0x100a);
    }
    function sm3(bytes data) public view returns (bytes32){
        return crypto.sm3(data);
    }

    function keccak256Hash(bytes data) public view returns (bytes32){
        return crypto.keccak256Hash(data);
    }

    function sm2Verify(bytes message, bytes sign) public view returns (bool, address){
        return crypto.sm2Verify(message, sign);
    }
}