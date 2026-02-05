#include "convert_app.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    try {
        ply2lcc::ConvertApp app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
