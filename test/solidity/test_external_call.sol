
pragma solidity <0.6.0;

contract B {
    constructor(int value) public {
        m_value = value;
    }
    
    function value() public view returns(int) {
        return m_value;
    }
    
    function incValue() public {
        ++m_value;
    }
    
    int m_value;
}

contract A {
    B b;
   
    function createAndCallB (int amount) public returns(int) {
        b = new B(amount);
        return b.value();
    }
}
