add_executable(intro main.cpp)

target_link_libraries(
  intro
  PRIVATE Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_options
          Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_warnings
          CLI11::CLI11
          fmt::fmt
          spdlog::spdlog
          lefticus::tools
          ftxui::screen
          ftxui::dom
          ftxui::component)


Darkest_Dungeon_Modloader_configure_linker(intro)

target_include_directories(intro PRIVATE "${CMAKE_BINARY_DIR}/configured_files/include")

# Apply WASM-specific configuration
if(EMSCRIPTEN)
  Darkest_Dungeon_Modloader_configure_wasm_target(intro
    TITLE "Intro - FTXUI Sample"
    DESCRIPTION "FTXUI-based terminal application"
  )
endif()
