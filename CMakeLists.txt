cmake_minimum_required(VERSION 3.20)

project(xcat2vtk VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(VTK REQUIRED COMPONENTS
  CommonCore
  CommonDataModel
  IOImage
  IOXML
)

add_executable(xcat2vtk
  src/main.cpp
  src/BinVolumeReader.cpp
  src/RawSurfaceReader.cpp
  src/VtkSceneWriter.cpp
)

target_include_directories(xcat2vtk PRIVATE src)

target_link_libraries(xcat2vtk PRIVATE ${VTK_LIBRARIES})

vtk_module_autoinit(
  TARGETS xcat2vtk
  MODULES ${VTK_LIBRARIES}
)
