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

add_library(xcat2vtk_core
  src/AnatomyClassifier.cpp
  src/BinVolumeReader.cpp
  src/RawSurfaceReader.cpp
  src/VtkSceneWriter.cpp
)

target_include_directories(xcat2vtk_core PUBLIC src)

target_link_libraries(xcat2vtk_core PUBLIC ${VTK_LIBRARIES})

add_executable(xcat2vtk
  src/main.cpp
)

target_link_libraries(xcat2vtk PRIVATE xcat2vtk_core)

enable_testing()

add_executable(xcat2vtk_anatomy_tests
  tests/test_anatomy_classifier.cpp
)

target_link_libraries(xcat2vtk_anatomy_tests PRIVATE xcat2vtk_core)

add_test(NAME anatomy_classifier COMMAND xcat2vtk_anatomy_tests)

vtk_module_autoinit(
  TARGETS xcat2vtk xcat2vtk_anatomy_tests
  MODULES ${VTK_LIBRARIES}
)
