cmake_minimum_required(VERSION 3.21)
project(StepViewer VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Third-party locations (populated by scripts/setup-deps.ps1)
set(DEPS_DIR "${CMAKE_SOURCE_DIR}/deps" CACHE PATH "Third-party root")
set(OpenCASCADE_DIR "${DEPS_DIR}/occt/opencascade-8.0.1-vc14-64/cmake" CACHE PATH "")
set(OCCT_3RDPARTY_DIR "${DEPS_DIR}/occt/3rdparty-vc14-64" CACHE PATH "")
list(APPEND CMAKE_PREFIX_PATH "${DEPS_DIR}/qt/6.8.3/msvc2022_64")

find_package(OpenCASCADE CONFIG REQUIRED
  COMPONENTS FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
add_compile_options(/utf-8 /MP /W3 /wd4996)

set(OUT_DIR "${CMAKE_BINARY_DIR}/out")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUT_DIR}")

# ---------------------------------------------------------------- core (shared by app + thumbnailer)
add_library(stepcore STATIC
  src/core/StepModel.cpp
  src/core/StepModel.h
  src/core/ThumbRenderer.cpp
  src/core/ThumbRenderer.h)
target_include_directories(stepcore PUBLIC src/core)
target_link_libraries(stepcore PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKShHealing
  TKMesh TKLCAF TKCAF TKXCAF TKDE TKXSBase TKDESTEP TKRWMesh TKService TKV3d TKVCAF)

# ---------------------------------------------------------------- viewer app
set(CMAKE_AUTOMOC ON)
add_executable(StepViewer WIN32
  src/app/main.cpp
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/app/OcctWidget.cpp
  src/app/OcctWidget.h
  src/app/Measure.cpp
  src/app/Measure.h
  resources/app.rc)
target_link_libraries(StepViewer PRIVATE stepcore Qt6::Widgets TKOpenGl TKBO)
set_target_properties(StepViewer PROPERTIES AUTOMOC ON)

# ---------------------------------------------------------------- Explorer thumbnail handler (COM in-proc server)
set(CMAKE_AUTOMOC OFF)
add_library(StepThumb SHARED
  src/thumb/ThumbProvider.cpp
  src/thumb/StepThumb.def
  resources/thumb.rc)
target_link_libraries(StepThumb PRIVATE stepcore shlwapi)

# ---------------------------------------------------------------- headless self-test (not shipped)
add_executable(selftest tools/selftest.cpp src/app/Measure.cpp)
target_include_directories(selftest PRIVATE src/app)
target_link_libraries(selftest PRIVATE stepcore Qt6::Core TKBO TKPrim)
set_target_properties(selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")

# ---------------------------------------------------------------- deploy runtime next to binaries
set(OCCT_BIN "${OpenCASCADE_BINARY_DIR}")
set(OCCT_RUNTIME_DLLS
  TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKTopAlgo TKPrim TKShHealing TKMesh TKHLR TKBO
  TKLCAF TKCAF TKCDF TKXCAF TKDE TKXSBase TKDESTEP TKRWMesh TKService TKV3d TKVCAF TKOpenGl)
set(_copy_cmds "")
foreach(dll ${OCCT_RUNTIME_DLLS})
  list(APPEND _copy_cmds COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OCCT_BIN}/${dll}.dll" "${OUT_DIR}")
endforeach()
# 3rd-party DLLs that the prebuilt OCCT binaries import (FFmpeg/OpenVR are linked by TKService but never used here)
file(GLOB THIRDPARTY_DLLS
  "${OCCT_3RDPARTY_DIR}/freetype-*/bin/freetype.dll"
  "${OCCT_3RDPARTY_DIR}/freeimage-*/bin/FreeImage.dll"
  "${OCCT_3RDPARTY_DIR}/tbb-*/bin/tbb12.dll"
  "${OCCT_3RDPARTY_DIR}/jemalloc-*/bin/jemalloc.dll"
  "${OCCT_3RDPARTY_DIR}/ffmpeg-*/bin/avcodec-*.dll"
  "${OCCT_3RDPARTY_DIR}/ffmpeg-*/bin/avformat-*.dll"
  "${OCCT_3RDPARTY_DIR}/ffmpeg-*/bin/avutil-*.dll"
  "${OCCT_3RDPARTY_DIR}/ffmpeg-*/bin/swscale-*.dll"
  "${OCCT_3RDPARTY_DIR}/openvr-*/bin/win64/openvr_api.dll")
foreach(dll ${THIRDPARTY_DLLS})
  list(APPEND _copy_cmds COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "${OUT_DIR}")
endforeach()
add_custom_target(deploy_runtime ALL ${_copy_cmds} COMMENT "Copying OCCT runtime")
add_dependencies(deploy_runtime StepViewer StepThumb)

get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qtbin "${_qmake}" DIRECTORY)
add_custom_command(TARGET StepViewer POST_BUILD
  COMMAND "${_qtbin}/windeployqt.exe" --release --no-translations --no-system-d3d-compiler
          --no-opengl-sw --no-system-dxc-compiler --no-compiler-runtime --skip-plugin-types imageformats,iconengines,generic,networkinformation,tls
          "$<TARGET_FILE:StepViewer>"
  COMMENT "Running windeployqt")
