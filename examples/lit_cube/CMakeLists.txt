# lit_cube: a cube shaded by the engine's lit path, for the desktop and for the web platform (main.cpp says more).
# Built from BRITE's own tree with the other examples (BRITE_BUILD_EXAMPLES).
add_executable(lit_cube main.cpp)
target_link_libraries(lit_cube PRIVATE BRITE::Raylib)

if(EMSCRIPTEN)
    # The web platform: a page to open (lit_cube.html beside its .js and .wasm), with Emscripten's default shell. The
    # heap grows as the program asks: what the engine and raylib allocate is not known up front.
    set_target_properties(lit_cube PROPERTIES SUFFIX ".html")
    target_link_options(lit_cube PRIVATE -sALLOW_MEMORY_GROWTH=1)
endif()
