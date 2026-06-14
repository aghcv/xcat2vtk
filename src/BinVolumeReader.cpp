#include "BinVolumeReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
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

bool ValidateBlock(const std::array<int, 3> &sourceDims,
                   const std::array<int, 3> &blockStart,
                   const std::array<int, 3> &blockDims,
                   std::string &error) {
  for (int axis = 0; axis < 3; ++axis) {
    if (sourceDims[axis] <= 0) {
      error = "Source volume dimensions must be positive.";
      return false;
    }
    if (blockDims[axis] <= 0) {
      error = "Sample block dimensions must be positive.";
      return false;
    }
    if (blockStart[axis] < 0) {
      error = "Sample block start index is outside the source volume.";
      return false;
    }
    if (blockStart[axis] > sourceDims[axis] - blockDims[axis]) {
      error = "Sample block extends outside the source volume.";
      return false;
    }
  }
  return true;
}

bool ReadBlockValues(const std::string &path,
                     const std::array<int, 3> &sourceDims,
                     const std::array<int, 3> &blockStart,
                     const std::array<int, 3> &blockDims,
                     std::vector<float> &values,
                     std::string &error) {
  if (!ValidateBlock(sourceDims, blockStart, blockDims, error)) {
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    error = "Cannot open volume file: " + path;
    return false;
  }

  size_t sourceVoxelCount = 0;
  size_t expectedBytes = 0;
  if (!ValidateFileSize(path, sourceDims, sourceVoxelCount, expectedBytes, error)) {
    return false;
  }
  (void)sourceVoxelCount;
  (void)expectedBytes;

  const size_t blockVoxelCount = static_cast<size_t>(blockDims[0]) *
                                 static_cast<size_t>(blockDims[1]) *
                                 static_cast<size_t>(blockDims[2]);
  values.assign(blockVoxelCount, 0.0f);

  const size_t rowValues = static_cast<size_t>(blockDims[0]);
  const size_t rowBytes = rowValues * sizeof(float);
  size_t destinationIndex = 0;
  for (int z = 0; z < blockDims[2]; ++z) {
    for (int y = 0; y < blockDims[1]; ++y) {
      const size_t sourceIndex =
          static_cast<size_t>(blockStart[0]) +
          static_cast<size_t>(sourceDims[0]) *
              (static_cast<size_t>(blockStart[1] + y) +
               static_cast<size_t>(sourceDims[1]) *
                   static_cast<size_t>(blockStart[2] + z));
      const std::streamoff offset =
          static_cast<std::streamoff>(sourceIndex * sizeof(float));
      file.seekg(offset, std::ios::beg);
      if (!file) {
        error = "Failed to seek volume file: " + path;
        return false;
      }
      file.read(reinterpret_cast<char *>(values.data() + destinationIndex), rowBytes);
      if (!file) {
        error = "Failed to read volume block from file: " + path;
        return false;
      }
      destinationIndex += rowValues;
    }
  }

  return true;
}

void ComputeStats(const std::vector<float> &values,
                  double backgroundValue,
                  double epsilon,
                  VolumeBlockStats &stats) {
  constexpr size_t distinctValueLimit = 4096;

  stats = VolumeBlockStats{};
  stats.voxelCount = values.size();
  if (values.empty()) {
    return;
  }

  std::set<float> distinctValues;
  double sum = 0.0;
  stats.minValue = static_cast<double>(values.front());
  stats.maxValue = static_cast<double>(values.front());
  for (const float value : values) {
    const double doubleValue = static_cast<double>(value);
    stats.minValue = std::min(stats.minValue, doubleValue);
    stats.maxValue = std::max(stats.maxValue, doubleValue);
    sum += doubleValue;
    if (!IsBackground(value, backgroundValue, epsilon)) {
      ++stats.nonBackgroundCount;
    }
    if (distinctValues.size() < distinctValueLimit) {
      distinctValues.insert(value);
    } else {
      stats.distinctValueLimitReached = true;
    }
  }
  stats.meanValue = sum / static_cast<double>(values.size());
  stats.distinctValueCount = distinctValues.size();
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

bool BinVolumeReader::ScanBlockStats(
    const std::string &path,
    const std::array<int, 3> &sourceDims,
    const std::array<int, 3> &blockStart,
    const std::array<int, 3> &blockDims,
    double backgroundValue,
    double epsilon,
    VolumeBlockStats &stats,
    std::string &error) {
  std::vector<float> values;
  if (!ReadBlockValues(path, sourceDims, blockStart, blockDims, values, error)) {
    return false;
  }
  ComputeStats(values, backgroundValue, epsilon, stats);
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

vtkSmartPointer<vtkImageData> BinVolumeReader::ReadBlock(
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
    std::string &error) {
  std::vector<float> values;
  if (!ReadBlockValues(path, sourceDims, blockStart, blockDims, values, error)) {
    return nullptr;
  }

  if (stats) {
    ComputeStats(values, backgroundValue, epsilon, *stats);
  }

  vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
  image->SetDimensions(blockDims[0] + 1, blockDims[1] + 1, blockDims[2] + 1);
  image->SetSpacing(spacing[0], spacing[1], spacing[2]);
  image->SetOrigin(origin[0] + static_cast<double>(blockStart[0]) * spacing[0],
                   origin[1] + static_cast<double>(blockStart[1]) * spacing[1],
                   origin[2] + static_cast<double>(blockStart[2]) * spacing[2]);

  vtkSmartPointer<vtkFloatArray> scalars = vtkSmartPointer<vtkFloatArray>::New();
  scalars->SetName(scalarName.c_str());
  scalars->SetNumberOfComponents(1);
  scalars->SetNumberOfTuples(values.size());
  std::memcpy(scalars->GetVoidPointer(0), values.data(), values.size() * sizeof(float));
  image->GetCellData()->SetScalars(scalars);

  return image;
}
