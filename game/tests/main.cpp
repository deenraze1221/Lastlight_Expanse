#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Bunker/BunkerGameplay.h"
#include <filesystem>
void writeBunkerRoute(const std::filesystem::path& output);
int main(int argc, char** argv) {
    lastlight::registerBunkerComponents();
    if (argc==3 && std::string_view(argv[1])=="--write-bunker-route") { writeBunkerRoute(argv[2]); return 0; }
    return doctest::Context(argc,argv).run();
}
