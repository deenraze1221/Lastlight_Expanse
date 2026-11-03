#include "Bunker/BunkerGameplay.h"

int zodarAssetToolMain(int argc, char** argv);
int main(int argc, char** argv) {
    lastlight::registerBunkerComponents();
    return zodarAssetToolMain(argc,argv);
}
