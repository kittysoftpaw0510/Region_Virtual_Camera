cmake_minimum_required(VERSION 3.24)
project(RegionCam LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(RegionCam
  src/main.cpp
  src/DesktopDuplication.cpp
  src/WindowCapture.cpp
  src/VirtualCamera.cpp
  src/OBSVirtualCam.cpp
  src/obs_queue/shared-memory-queue.c
)

target_include_directories(RegionCam PRIVATE src)

# Windows/WinRT & Media Foundation & DXGI/D3D11
target_link_libraries(RegionCam
  d3d11
  dxgi
  dxguid
  Mfplat
  Mfreadwrite
  Mf
  Mfuuid
  ole32
  uuid
  Shcore
  Gdiplus
  User32
  Gdi32
  Dwmapi
)

# High DPI awareness
if (MSVC)
  target_compile_definitions(RegionCam PRIVATE UNICODE _UNICODE NOMINMAX)
  target_link_options(RegionCam PRIVATE /MANIFESTUAC:NO)
endif()
