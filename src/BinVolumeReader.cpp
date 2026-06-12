#include "BinVolumeReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkImageData.h>

namespace {
bool ValidateFileSize(const std::string &path,
                      const std::array<int, 3> &dims,
                      size_t &voxelCount,
                      size_t &expectedBytes,
                      std::string &error) {
  voxelCount = static_cast<size_t>(dims[0]) *
               static_cast<size_t>(dims[1]) *
               static_cast<size_t>(dims[2]);
  expectedBytes = voxelCount * sizeof(float);

  std::error_code fsError;
  const size_t fileBytes = std::filesystem::file_size(path, fsError);
  if (fsError) {
    error = "Cannot read volume file size: " + path;
    return false;
  }
  if (fileBytes != expectedBytes) {
    error = "Binary size mismatch for volume file: " + path;
    return false;
  }
  return true;
}

bool IsBackground(float value, double backgroundValue, double epsilon) {
  return std::abs(static_cast<double>(value) - backgroundValue) <= epsilon;
}

void IncludeVoxelIndex(VolumeIndexBounds &bounds,
                       const std::array<int, 3> &dims,
                       size_t linearIndex) {
  const int x = static_cast<int>(linearIndex % static_cast<size_t>(dims[0]));
  const int y = static_cast<int>((linearIndex / static_cast<size_t>(dims[0])) %
                                 static_cast<size_t>(dims[1]));
  const int z = static_cast<int>(linearIndex /
                                 (static_cast<size_t>(dims[0]) *
                                  static_cast<size_t>(dims[1])));

  if (!bounds.valid) {
    bounds.valid = true;
    bounds.min = {x, y, z};
    bounds.max = {x, y, z};
    return;
  }

  bounds.min[0] = std::min(bounds.min[0], x);
  bounds.min[1] = std::min(bounds.min[1], y);
  bounds.min[2] = std::min(bounds.min[2], z);
  bounds.max[0] = std::max(bounds.max[0], x);
  bounds.max[1] = std::max(bounds.max[1], y);
  bounds.max[2] = std::max(bounds.max[2], z);
}
} // namespace

bool BinVolumeReader::ScanNonBackgroundBounds(
    const std::string &path,
    const std::array<int, 3> &dims,
    double backgroundValue,
    double epsilon,
    VolumeIndexBounds &bounds,
    std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    error = "Cannot open volume file: " + path;
    return false;
  }

  size_t voxelCount = 0;
  size_t expectedBytes = 0;
  if (!ValidateFileSize(path, dims, voxelCount, expectedBytes, error)) {
    return false;
  }

  constexpr size_t chunkValues = 1024 * 1024;
  std::vector<float> buffer(chunkValues);
  size_t linearIndex = 0;
  while (linearIndex < voxelCount) {
    const size_t remaining = voxelCount - linearIndex;
    const size_t valuesToRead = std::min(buffer.size(), remaining);
    const size_t bytesToRead = valuesToRead * sizeof(float);
    file.read(reinterpret_cast<char *>(buffer.data()), bytesToRead);
    if (!file) {
      error = "Failed to read volume file: " + path;
      return false;
    }

    for (size_t i = 0; i < valuesToRead; ++i) {
      if (!IsBackground(buffer[i], backgroundValue, epsilon)) {
        IncludeVoxelIndex(bounds, dims, linearIndex + i);
      }
    }
    linearIndex += valuesToRead;
  }

  return true;
}

vtkSmartPointer<vtkImageData> BinVolumeReader::Read(
    const std::string &path,
    const std::array<int, 3> &dims,
    const std::array<double, 3> &spacing,
    const std::array<double, 3> &origin,
    const std::string &scalarName,
    std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    error = "Cannot open volume file: " + path;
    return nullptr;
  }

  size_t voxelCount = 0;
  size_t expectedBytes = 0;
  if (!ValidateFileSize(path, dims, voxelCount, expectedBytes, error)) {
    return nullptr;
  }

  std::vector<float> buffer(voxelCount);
  file.read(reinterpret_cast<char *>(buffer.data()), expectedBytes);
  if (!file) {
    error = "Failed to read volume file: " + path;
    return nullptr;
  }

  vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
  image->SetDimensions(dims[0] + 1, dims[1] + 1, dims[2] + 1);
  image->SetSpacing(spacing[0], spacing[1], spacing[2]);
  image->SetOrigin(origin[0], origin[1], origin[2]);

  vtkSmartPointer<vtkFloatArray> scalars = vtkSmartPointer<vtkFloatArray>::New();
  scalars->SetName(scalarName.c_str());
  scalars->SetNumberOfComponents(1);
  scalars->SetNumberOfTuples(voxelCount);
  std::memcpy(scalars->GetVoidPointer(0), buffer.data(), expectedBytes);
  image->GetCellData()->SetScalars(scalars);

  return image;
}
