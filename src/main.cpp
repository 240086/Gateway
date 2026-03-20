#include "gateway/core/GatewayServer.h"
#include <boost/asio.hpp>
#include <iostream>

int main()
{
    try
    {
        boost::asio::io_context io;

        GatewayServer server(io, 9000);
        server.Start();

        std::cout << "[Gateway] Running on port 9000..." << std::endl;

        io.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}