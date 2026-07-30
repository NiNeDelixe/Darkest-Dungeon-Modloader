include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)


include(CheckCXXSourceCompiles)


macro(Darkest_Dungeon_Modloader_supports_sanitizers)
  # Emscripten doesn't support sanitizers
  if(EMSCRIPTEN)
    set(SUPPORTS_UBSAN OFF)
    set(SUPPORTS_ASAN OFF)
  elseif((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)

    message(STATUS "Sanity checking UndefinedBehaviorSanitizer, it should be supported on this platform")
    set(TEST_PROGRAM "int main() { return 0; }")

    # Check if UndefinedBehaviorSanitizer works at link time
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("${TEST_PROGRAM}" HAS_UBSAN_LINK_SUPPORT)

    if(HAS_UBSAN_LINK_SUPPORT)
      message(STATUS "UndefinedBehaviorSanitizer is supported at both compile and link time.")
      set(SUPPORTS_UBSAN ON)
    else()
      message(WARNING "UndefinedBehaviorSanitizer is NOT supported at link time.")
      set(SUPPORTS_UBSAN OFF)
    endif()
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    if (NOT WIN32)
      message(STATUS "Sanity checking AddressSanitizer, it should be supported on this platform")
      set(TEST_PROGRAM "int main() { return 0; }")

      # Check if AddressSanitizer works at link time
      set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
      set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
      check_cxx_source_compiles("${TEST_PROGRAM}" HAS_ASAN_LINK_SUPPORT)

      if(HAS_ASAN_LINK_SUPPORT)
        message(STATUS "AddressSanitizer is supported at both compile and link time.")
        set(SUPPORTS_ASAN ON)
      else()
        message(WARNING "AddressSanitizer is NOT supported at link time.")
        set(SUPPORTS_ASAN OFF)
      endif()
    else()
      set(SUPPORTS_ASAN ON)
    endif()
  endif()
endmacro()

macro(Darkest_Dungeon_Modloader_setup_options)
  option(Darkest_Dungeon_Modloader_ENABLE_HARDENING "Enable hardening" ON)
  option(Darkest_Dungeon_Modloader_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    Darkest_Dungeon_Modloader_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    Darkest_Dungeon_Modloader_ENABLE_HARDENING
    OFF)

  Darkest_Dungeon_Modloader_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR Darkest_Dungeon_Modloader_PACKAGING_MAINTAINER_MODE)
    option(Darkest_Dungeon_Modloader_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_PCH "Enable precompiled headers" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CACHE "Enable ccache" OFF)
  elseif(ENABLE_DEVELOPER_MODE)
    option(Darkest_Dungeon_Modloader_ENABLE_IPO "Enable IPO/LTO" ON)
    option(Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
    option(Darkest_Dungeon_Modloader_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(Darkest_Dungeon_Modloader_ENABLE_PCH "Enable precompiled headers" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CACHE "Enable ccache" ON)
  else()
    option(Darkest_Dungeon_Modloader_ENABLE_IPO "Enable IPO/LTO" ON)
    option(Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_PCH "Enable precompiled headers" OFF)
    option(Darkest_Dungeon_Modloader_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      Darkest_Dungeon_Modloader_ENABLE_IPO
      Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS
      Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS
      Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK
      Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED
      Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD
      Darkest_Dungeon_Modloader_ENABLE_SANITIZER_MEMORY
      Darkest_Dungeon_Modloader_ENABLE_UNITY_BUILD
      Darkest_Dungeon_Modloader_ENABLE_CLANG_TIDY
      Darkest_Dungeon_Modloader_ENABLE_CPPCHECK
      Darkest_Dungeon_Modloader_ENABLE_LIZARD
      Darkest_Dungeon_Modloader_ENABLE_BLOATY
      Darkest_Dungeon_Modloader_ENABLE_COVERAGE
      Darkest_Dungeon_Modloader_ENABLE_PCH
      Darkest_Dungeon_Modloader_ENABLE_CACHE)
  endif()

  Darkest_Dungeon_Modloader_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED AND (Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(Darkest_Dungeon_Modloader_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(Darkest_Dungeon_Modloader_global_options)
  if(Darkest_Dungeon_Modloader_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    Darkest_Dungeon_Modloader_enable_ipo()
  endif()

  Darkest_Dungeon_Modloader_supports_sanitizers()

  if(Darkest_Dungeon_Modloader_ENABLE_HARDENING AND Darkest_Dungeon_Modloader_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${Darkest_Dungeon_Modloader_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED}")
    Darkest_Dungeon_Modloader_enable_hardening(Darkest_Dungeon_Modloader_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(Darkest_Dungeon_Modloader_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(Darkest_Dungeon_Modloader_warnings INTERFACE)
  add_library(Darkest_Dungeon_Modloader_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  Darkest_Dungeon_Modloader_set_project_warnings(
    Darkest_Dungeon_Modloader_warnings
    ${Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  include(cmake/Linker.cmake)
  # Must configure each target with linker options, we're avoiding setting it globally for now

  if(NOT EMSCRIPTEN)
    include(cmake/Sanitizers.cmake)
    Darkest_Dungeon_Modloader_enable_sanitizers(
      Darkest_Dungeon_Modloader_options
      ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS}
      ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK}
      ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED}
      ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD}
      ${Darkest_Dungeon_Modloader_ENABLE_SANITIZER_MEMORY})
  endif()

  set_target_properties(Darkest_Dungeon_Modloader_options PROPERTIES UNITY_BUILD ${Darkest_Dungeon_Modloader_ENABLE_UNITY_BUILD})

  if(Darkest_Dungeon_Modloader_ENABLE_PCH)
    target_precompile_headers(
      Darkest_Dungeon_Modloader_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(Darkest_Dungeon_Modloader_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    Darkest_Dungeon_Modloader_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(Darkest_Dungeon_Modloader_ENABLE_CLANG_TIDY)
    Darkest_Dungeon_Modloader_enable_clang_tidy(Darkest_Dungeon_Modloader_options ${Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS})
  endif()

  if(Darkest_Dungeon_Modloader_ENABLE_CPPCHECK)
    Darkest_Dungeon_Modloader_enable_cppcheck(${Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()
  
  if(Darkest_Dungeon_Modloader_ENABLE_LIZARD)
    Darkest_Dungeon_Modloader_enable_lizard(${Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS})
  endif()
  
  if(Darkest_Dungeon_Modloader_ENABLE_BLOATY)
    Darkest_Dungeon_Modloader_enable_bloaty()
  endif()

  if(Darkest_Dungeon_Modloader_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    Darkest_Dungeon_Modloader_enable_coverage(Darkest_Dungeon_Modloader_options)
  endif()

  if(Darkest_Dungeon_Modloader_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(Darkest_Dungeon_Modloader_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(Darkest_Dungeon_Modloader_ENABLE_HARDENING AND NOT Darkest_Dungeon_Modloader_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_UNDEFINED
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_ADDRESS
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_THREAD
       OR Darkest_Dungeon_Modloader_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    Darkest_Dungeon_Modloader_enable_hardening(Darkest_Dungeon_Modloader_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
