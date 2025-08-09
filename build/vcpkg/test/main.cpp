#include <iostream>

#include <opros/wait_set.hpp>

int main(int argc, const char** argv){
    opros::wait_set ws(2);

    std::cout << "ws.size() = " << ws.size() << std::endl;

    return 0;
}
