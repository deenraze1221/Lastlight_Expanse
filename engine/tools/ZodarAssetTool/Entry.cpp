// The generic tool and game-specific frontends share the same command implementation.
int zodarAssetToolMain(int argc, char** argv);
int main(int argc, char** argv) { return zodarAssetToolMain(argc,argv); }
