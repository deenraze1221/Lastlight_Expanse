# Each engine module is a separate static library. Dependencies only point downwards:
#   Core  <-  Assets (CPU data, importers)
#   Core  <-  Platform
#   Assets + Platform  <-  Renderer
add_subdirectory(Core)
add_subdirectory(Assets)
add_subdirectory(Platform)
add_subdirectory(Renderer)
