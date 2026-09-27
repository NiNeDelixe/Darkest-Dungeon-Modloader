include(GenerateExportHeader)


# set(MAIN_SOURCES
#   main.cpp
# )

#add_executable(darkest1_modloader ${MAIN_SOURCES})
add_library(ExampleMod SHARED
    ExampleMod.cpp
)

target_compile_features(ExampleMod PUBLIC cxx_std_17)

target_link_libraries(
  ExampleMod
  PRIVATE Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_options
          Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_warnings
          minhook
)


Darkest_Dungeon_Modloader_configure_linker(ExampleMod)

target_include_directories(ExampleMod PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include" ${CMAKE_SOURCE_DIR}/SDL2-2.0.4/include)

# Apply WASM-specific configuration
if(EMSCRIPTEN)
  Darkest_Dungeon_Modloader_configure_wasm_target(ExampleMod
    TITLE "ExampleMod - FTXUI Sample"
    DESCRIPTION "FTXUI-based terminal application"
  )
endif()
