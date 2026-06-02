#pragma once

#include <string>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct SurfaceData {
  std::string group;
  std::string label;
  vtkSmartPointer<vtkPolyData> data;
};
