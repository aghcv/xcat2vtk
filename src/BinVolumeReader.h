#pragma once

#include <array>
#include <string>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

class BinVolumeReader {
public:
  static vtkSmartPointer<vtkImageData> Read(
      const std::string &path,
      const std::array<int, 3> &dims,
      const std::array<double, 3> &spacing,
      const std::array<double, 3> &origin,
      const std::string &scalarName,
      std::string &error);
};
