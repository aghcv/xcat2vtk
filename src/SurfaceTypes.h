#pragma once

#include <string>
#include <array>
#include <vector>

#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct SurfaceData {
  std::string group;
  std::string label;
  vtkSmartPointer<vtkPolyData> data;
};

struct SurfaceLabel {
  std::string group;
  std::string label;
};

struct SurfaceBounds {
  bool valid = false;
  std::array<double, 3> min{0.0, 0.0, 0.0};
  std::array<double, 3> max{0.0, 0.0, 0.0};
};

struct SurfaceScanResult {
  std::vector<SurfaceLabel> labels;
  SurfaceBounds bounds;
};
