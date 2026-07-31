include(GenerateExportHeader)


# set(MAIN_SOURCES
#   main.cpp
# )

#add_executable(sdl2_modloader ${MAIN_SOURCES})
add_library(SDL2 SHARED
    dllmain.cpp
    exports.cpp
)

add_library(sdl2_modloader ALIAS SDL2)

target_link_libraries(
  SDL2
  PRIVATE Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_options
          Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_warnings
)


Darkest_Dungeon_Modloader_configure_linker(SDL2)

target_include_directories(SDL2 PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")

# Apply WASM-specific configuration
if(EMSCRIPTEN)
  Darkest_Dungeon_Modloader_configure_wasm_target(SDL2
    TITLE "SDL2 - FTXUI Sample"
    DESCRIPTION "FTXUI-based terminal application"
  )
endif()
