#include "BinVolumeReader.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <vtkFloatArray.h>
#include <vtkImageData.h>
#include <vtkPointData.h>

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

  const size_t voxelCount = static_cast<size_t>(dims[0]) *
                            static_cast<size_t>(dims[1]) *
                            static_cast<size_t>(dims[2]);
  const size_t expectedBytes = voxelCount * sizeof(float);

  std::error_code fsError;
  const size_t fileBytes = std::filesystem::file_size(path, fsError);
  if (fsError) {
    error = "Cannot read volume file size: " + path;
    return nullptr;
  }
  if (fileBytes != expectedBytes) {
    error = "Binary size mismatch for volume file: " + path;
    return nullptr;
  }

  std::vector<float> buffer(voxelCount);
  file.read(reinterpret_cast<char *>(buffer.data()), expectedBytes);
  if (!file) {
    error = "Failed to read volume file: " + path;
    return nullptr;
  }

  vtkSmartPointer<vtkImageData> image = vtkSmartPointer<vtkImageData>::New();
  image->SetDimensions(dims[0], dims[1], dims[2]);
  image->SetSpacing(spacing[0], spacing[1], spacing[2]);
  image->SetOrigin(origin[0], origin[1], origin[2]);
  image->AllocateScalars(VTK_FLOAT, 1);

  vtkFloatArray *scalars = vtkFloatArray::SafeDownCast(image->GetPointData()->GetScalars());
  if (!scalars) {
    error = "Failed to allocate VTK scalars for volume: " + path;
    return nullptr;
  }
  scalars->SetName(scalarName.c_str());
  std::memcpy(scalars->GetVoidPointer(0), buffer.data(), expectedBytes);

  return image;
}
