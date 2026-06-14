#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

struct VolumeIndexBounds {
  bool valid = false;
  std::array<int, 3> min{0, 0, 0};
  std::array<int, 3> max{0, 0, 0};
};

struct VolumeBlockStats {
  size_t voxelCount = 0;
  size_t nonBackgroundCount = 0;
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  size_t distinctValueCount = 0;
  bool distinctValueLimitReached = false;
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

  static bool ScanBlockStats(
      const std::string &path,
      const std::array<int, 3> &sourceDims,
      const std::array<int, 3> &blockStart,
      const std::array<int, 3> &blockDims,
      double backgroundValue,
      double epsilon,
      VolumeBlockStats &stats,
      std::string &error);

  static vtkSmartPointer<vtkImageData> Read(
      const std::string &path,
      const std::array<int, 3> &dims,
      const std::array<double, 3> &spacing,
      const std::array<double, 3> &origin,
      const std::string &scalarName,
      std::string &error);

  static vtkSmartPointer<vtkImageData> ReadBlock(
      const std::string &path,
      const std::array<int, 3> &sourceDims,
      const std::array<double, 3> &spacing,
      const std::array<double, 3> &origin,
      const std::array<int, 3> &blockStart,
      const std::array<int, 3> &blockDims,
      const std::string &scalarName,
      double backgroundValue,
      double epsilon,
      VolumeBlockStats *stats,
      std::string &error);
};
