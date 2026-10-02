include(GenerateExportHeader)

set(Python3_EXECUTABLE "python")
set(INPUT_FUNCS_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/dev/scripts/dllopengl32functions.txt")
set(GENERATOR_SCRIPT  "${CMAKE_CURRENT_SOURCE_DIR}/dev/scripts/generate_forwarders.py")
set(GENERATED_DIR     "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_HEADER  "${GENERATED_DIR}/export_forwarders.h")

# Name of the original DLL to forward to.
# Can be overridden on the command line: -DOPENGL32_ORIG=OPENGL32_orig
set(OPENGL32_ORIG "C:/WINDOWS/system32/OPENGL32" CACHE STRING "Name of the original OpenGL32 DLL")

# Functions we do NOT forward (they will be hooked manually).
# Can be extended on the command line: -DEXCLUDE_FUNCS="glDrawElements;glDrawArrays"
set(EXCLUDE_FUNCS "" CACHE STRING "Functions to exclude from forwarding (semicolon-separated)")

# Convert the CMake list "a;b;c" into "a,b,c" for Python
string(REPLACE ";" "," EXCLUDE_FUNCS_CSV "${EXCLUDE_FUNCS}")

add_custom_command(
    OUTPUT  "${GENERATED_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
            "${GENERATOR_SCRIPT}"
            --input    "${INPUT_FUNCS_FILE}"
            --output   "${GENERATED_HEADER}"
            --original "${OPENGL32_ORIG}"
            --exclude  "${EXCLUDE_FUNCS_CSV}"
    DEPENDS
        "${GENERATOR_SCRIPT}"
        "${INPUT_FUNCS_FILE}"
    COMMENT "Generating OpenGL32 export forwarders via Python"
    VERBATIM
)

add_custom_target(generate_forwarders DEPENDS "${GENERATED_HEADER}")

message(STATUS "==========================================================")
message(STATUS " Opengl32 Proxy configuration")
message(STATUS "   Python:           ${Python3_EXECUTABLE}")
message(STATUS "   Input functions:  ${INPUT_FUNCS_FILE}")
message(STATUS "   Generated header: ${GENERATED_HEADER}")
message(STATUS "   Original DLL:     ${OPENGL32_ORIG}")
message(STATUS "   Exclude funcs:    '${EXCLUDE_FUNCS}'")
message(STATUS "==========================================================")


# SOURCES

set(HOOK_SOURCES
  hooks/D3D11Hook.cpp
  hooks/WindowsMessageHook.cpp
  hooks/WindowFilter.cpp
  hooks/GLHook.cpp
)

set(MODLOADER_SOURCES
  ModLoader.cpp
  Darkest1Modloader.cpp
)

set(MAIN_SOURCES
  main.cpp
)

set(ALL_SOURCES
  ${MAIN_SOURCES}
  ${MODLOADER_SOURCES}
  ${HOOK_SOURCES}
)

add_library(darkest1_modloader SHARED
    ${ALL_SOURCES}

    "${GENERATED_HEADER}"
)

add_dependencies(darkest1_modloader generate_forwarders)

set_target_properties(darkest1_modloader PROPERTIES OUTPUT_NAME "OPENGL32")

target_link_libraries(
  darkest1_modloader
  PRIVATE 
    Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_options
    Darkest_Dungeon_Modloader::Darkest_Dungeon_Modloader_warnings
    minhook
    Boost::asio 
    Boost::container
    Boost::dll
    Boost::filesystem
    Boost::log
    Boost::property_tree
    spdlog::spdlog
    imgui
    imguizmo::imguizmo
    cpptrace::cpptrace
    bddisasm::bddisasm
    kananlib

    DirectXTK
		DirectXTK12
    d3d11
)


Darkest_Dungeon_Modloader_configure_linker(darkest1_modloader)

target_include_directories(darkest1_modloader 
  PRIVATE 
    "${CMAKE_BINARY_DIR}/configured_files/include"
    "${GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/win"
)

# Apply WASM-specific configuration
# if(EMSCRIPTEN)
#   Darkest_Dungeon_Modloader_configure_wasm_target(darkest1_modloader
#     TITLE "darkest1_modloader - FTXUI Sample"
#     DESCRIPTION "FTXUI-based terminal application"
#   )
# endif()
