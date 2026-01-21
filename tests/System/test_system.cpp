#include <iostream>
#include <System/Dispatcher.h>
#include <System/TcpListener.h>
#include <System/Ipv4Address.h>

int main() {
    try {
        std::cout << "Creating dispatcher..." << std::endl;
        System::Dispatcher dispatcher;
        std::cout << "Creating listener..." << std::endl;
        System::TcpListener listener(dispatcher, System::Ipv4Address("127.0.0.1"), 28281);
        std::cout << "Success!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
