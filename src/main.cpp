#include "convert_app.hpp"
#include "platform.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    try {
        auto args = platform::utf8_argv(argc, argv);
        ply2lcc::ConvertApp app(args.argc, args.argv.data());
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
