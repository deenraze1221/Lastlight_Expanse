# Each engine module is a separate static library. Dependencies only point downwards:
#   Core  <-  Platform  <-  Renderer
add_subdirectory(Core)
add_subdirectory(Platform)
add_subdirectory(Renderer)
