include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(Darkest_Dungeon_Modloader_setup_dependencies)

  # For each dependency, see if it's
  # already been provided to us by a parent project

  if(NOT TARGET fmtlib::fmtlib)
    cpmaddpackage(
      NAME
      fmt
      GITHUB_REPOSITORY
      "fmtlib/fmt"
      GIT_TAG
      "12.1.0"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET spdlog::spdlog)
    cpmaddpackage(
      NAME
      spdlog
      VERSION
      1.17.0
      GITHUB_REPOSITORY
      "gabime/spdlog"
      SYSTEM
      YES
      OPTIONS
      "SPDLOG_FMT_EXTERNAL ON")
  endif()

  if(NOT TARGET Catch2::Catch2WithMain)
    cpmaddpackage(
      NAME
      Catch2
      VERSION
      3.12.0
      GITHUB_REPOSITORY
      "catchorg/Catch2"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET CLI11::CLI11)
    cpmaddpackage(
      NAME
      CLI11
      VERSION
      2.6.1
      GITHUB_REPOSITORY
      "CLIUtils/CLI11"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET ftxui::screen)
    cpmaddpackage(
      NAME
      FTXUI
      VERSION
      6.1.9
      GITHUB_REPOSITORY
      "ArthurSonzogni/FTXUI"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET tools::tools)
    cpmaddpackage(
      NAME
      tools
      GITHUB_REPOSITORY
      "lefticus/tools"
      GIT_TAG
      "main")
  endif()

  if(NOT TARGET minhook)
    cpmaddpackage(
      NAME
      minhook
      VERSION
      1.3.4
      GITHUB_REPOSITORY
      "TsudaKageyu/minhook"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET boost)
    CPMAddPackage(
      NAME Boost
      VERSION 1.86.0 # Versions less than 1.85.0 may need patches for installation targets.
      URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
      URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
      OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
              "BUILD_SHARED_LIBS OFF" "BOOST_INCLUDE_LIBRARIES container\\\;asio\\\;dll\\\;filesystem\\\;log\\\;property_tree" # Note the escapes!
    )
  endif()
  
  

  # if(NOT TARGET sdl2)
  #   #set(SDL_STATIC OFF)
  #   cpmaddpackage(
  #     NAME
  #     sdl2
  #     GITHUB_REPOSITORY
  #     "libsdl-org/SDL"
  #     GIT_TAG
  #     "release-2.0.4"
  #     OPTIONS
  #     "SDL_STATIC OFF")
  # endif()

endfunction()
