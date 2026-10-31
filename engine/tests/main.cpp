#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Core/Log.h"

int main(int argc, char** argv) {
    zodar::log::init({spdlog::level::warn, ""});
    doctest::Context context(argc, argv);
    const int result = context.run();
    zodar::log::shutdown();
    return result;
}
