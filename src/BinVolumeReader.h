#pragma once

#include <array>
#include <string>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

struct VolumeIndexBounds {
  bool valid = false;
  std::array<int, 3> min{0, 0, 0};
  std::array<int, 3> max{0, 0, 0};
};

class BinVolumeReader {
public:
  static bool ScanNonBackgroundBounds(
      const std::string &path,
      const std::array<int, 3> &dims,
      double backgroundValue,
      double epsilon,
      VolumeIndexBounds &bounds,
      std::string &error);

  static vtkSmartPointer<vtkImageData> Read(
      const std::string &path,
      const std::array<int, 3> &dims,
      const std::array<double, 3> &spacing,
      const std::array<double, 3> &origin,
      const std::string &scalarName,
      std::string &error);
};
