include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(Darkest_Dungeon_Modloader_setup_dependencies)

  if(NOT DEFINED CPM_SOURCE_CACHE)
    set(CPM_SOURCE_CACHE "${CMAKE_BINARY_DIR}/.cache")
  endif()
  

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

  if(NOT TARGET imgui)
    cpmaddpackage(
      NAME
      imgui
      VERSION
      1.92.9b
      GITHUB_REPOSITORY
      "ocornut/imgui"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET cpptrace)
    cpmaddpackage(
      NAME
      cpptrace
      VERSION
      1.0.4
      GITHUB_REPOSITORY
      "jeremy-rifkin/cpptrace"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET directxtk)
    cpmaddpackage(
      NAME
      directxtk
      GIT_TAG
      f5026eb34e7053b1aff325d38db107703f394974
      GITHUB_REPOSITORY
      "microsoft/DirectXTK"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET directxtk12)

    if(NOT DEFINED DIRECTX_ARCH)
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
          set(DIRECTX_ARCH "x64")
      else()
          set(DIRECTX_ARCH "x86")
      endif()
    endif()

    cpmaddpackage(
      NAME
      directxtk12
      GIT_TAG
      be5dfc7e391aefaa4eeab8e8a08f7e2669e56cbb
      GITHUB_REPOSITORY
      "microsoft/DirectXTK12"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET bddisasm)
    cpmaddpackage(
      NAME
      bddisasm
      VERSION
      1.37.0
      GITHUB_REPOSITORY
      "bitdefender/bddisasm"
      SYSTEM
      YES)
  endif()

  if(NOT TARGET kananlib)
    cpmaddpackage(
      NAME
      kananlib
      GIT_TAG
      43cb5353615dcbc5d4945b1b9ab685b11e8b0d37
      GITHUB_REPOSITORY
      "cursey/kananlib"
      SYSTEM
      YES)
  endif()



  if(kananlib_ADDED)
    target_link_libraries(kananlib PUBLIC bddisasm)
    target_include_directories(kananlib PUBLIC
      ${bddisasm_SOURCE_DIR}/inc
    )
  endif()
  

  if(imgui_ADDED)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
      
    target_link_libraries(imgui PUBLIC DirectXTK DirectXTK12)
  endif()

  if(NOT TARGET imguizmo)
    cpmaddpackage(
      NAME
      imguizmo
      GIT_TAG
      b796ac3b861afc6e91ca74e4611effd9c9527367
      GITHUB_REPOSITORY
      "CedricGuillemet/ImGuizmo"
      SYSTEM
      YES
    )

    if(imguizmo_ADDED)
      target_link_libraries(imguizmo PUBLIC imgui)
      target_include_directories(imguizmo PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
      )
    endif()
  endif()
  
endfunction()
