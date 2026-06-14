#include "RawSurfaceReader.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>

namespace {
bool TryParseFloat(const std::string &token, float &value) {
  char *end = nullptr;
  value = std::strtof(token.c_str(), &end);
  if (end == token.c_str() || *end != '\0') {
    return false;
  }
  return true;
}

void IncludePoint(SurfaceBounds &bounds,
                  const std::array<double, 3> &scale,
                  const std::array<double, 3> &translate,
                  const std::array<float, 3> &point) {
  const std::array<double, 3> transformed{
      static_cast<double>(point[0]) * scale[0] + translate[0],
      static_cast<double>(point[1]) * scale[1] + translate[1],
      static_cast<double>(point[2]) * scale[2] + translate[2]};

  if (!bounds.valid) {
    bounds.valid = true;
    bounds.min = transformed;
    bounds.max = transformed;
    return;
  }

  for (int axis = 0; axis < 3; ++axis) {
    bounds.min[axis] = std::min(bounds.min[axis], transformed[axis]);
    bounds.max[axis] = std::max(bounds.max[axis], transformed[axis]);
  }
}

vtkSmartPointer<vtkPolyData> BuildPolyData(const std::vector<float> &values) {
  vtkSmartPointer<vtkPolyData> polyData = vtkSmartPointer<vtkPolyData>::New();
  vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
  vtkSmartPointer<vtkCellArray> polys = vtkSmartPointer<vtkCellArray>::New();

  for (size_t i = 0; i < values.size(); i += 9) {
    vtkIdType ids[3];
    ids[0] = points->InsertNextPoint(values[i + 0], values[i + 1], values[i + 2]);
    ids[1] = points->InsertNextPoint(values[i + 3], values[i + 4], values[i + 5]);
    ids[2] = points->InsertNextPoint(values[i + 6], values[i + 7], values[i + 8]);
    polys->InsertNextCell(3, ids);
  }

  polyData->SetPoints(points);
  polyData->SetPolys(polys);
  return polyData;
}

void ApplyTransform(vtkSmartPointer<vtkPolyData> polyData,
                    const std::array<double, 3> &scale,
                    const std::array<double, 3> &translate) {
  if (!polyData) {
    return;
  }
  if (scale[0] == 1.0 && scale[1] == 1.0 && scale[2] == 1.0 &&
      translate[0] == 0.0 && translate[1] == 0.0 && translate[2] == 0.0) {
    return;
  }
  vtkPoints *points = polyData->GetPoints();
  if (!points) {
    return;
  }
  const vtkIdType count = points->GetNumberOfPoints();
  double p[3];
  for (vtkIdType i = 0; i < count; ++i) {
    points->GetPoint(i, p);
    p[0] = p[0] * scale[0] + translate[0];
    p[1] = p[1] * scale[1] + translate[1];
    p[2] = p[2] * scale[2] + translate[2];
    points->SetPoint(i, p);
  }
  points->Modified();
}
} // namespace

bool RawSurfaceReader::ScanAll(
    const std::string &path,
    const std::string &groupLabel,
    const std::array<double, 3> &scale,
    const std::array<double, 3> &translate,
    SurfaceScanResult &result,
    std::string &error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Cannot open surface file: " + path;
    return false;
  }

  const std::string fallbackLabel = groupLabel.empty() ? "surface" : groupLabel;
  std::string currentLabel = fallbackLabel;
  size_t currentValueCount = 0;
  size_t axis = 0;
  std::array<float, 3> point{0.0f, 0.0f, 0.0f};
  std::string token;
  bool sawNumeric = false;

  auto flushSurface = [&]() -> bool {
    if (currentValueCount == 0) {
      return true;
    }
    if (currentValueCount % 9 != 0) {
      error = "Surface data does not contain a multiple of 9 numeric values: " +
              currentLabel + " in " + path;
      return false;
    }
    result.labels.push_back({groupLabel, currentLabel.empty() ? fallbackLabel : currentLabel});
    currentValueCount = 0;
    axis = 0;
    return true;
  };

  while (file >> token) {
    float value = 0.0f;
    if (TryParseFloat(token, value)) {
      point[axis] = value;
      ++axis;
      ++currentValueCount;
      sawNumeric = true;
      if (axis == 3) {
        IncludePoint(result.bounds, scale, translate, point);
        axis = 0;
      }
      continue;
    }

    if (!flushSurface()) {
      return false;
    }
    currentLabel = token;
  }

  if (!flushSurface()) {
    return false;
  }

  if (!sawNumeric) {
    error = "Surface file does not contain any numeric data: " + path;
    return false;
  }

  return true;
}

bool RawSurfaceReader::ReadAll(
    const std::string &path,
    const std::string &groupLabel,
    const std::array<double, 3> &scale,
    const std::array<double, 3> &translate,
    std::vector<SurfaceData> &surfaces,
    std::string &error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Cannot open surface file: " + path;
    return false;
  }

  const std::string fallbackLabel = groupLabel.empty() ? "surface" : groupLabel;
  std::string currentLabel = fallbackLabel;
  std::vector<float> currentValues;
  std::string token;
  bool sawNumeric = false;

  auto flushSurface = [&]() -> bool {
    if (currentValues.empty()) {
      return true;
    }
    if (currentValues.size() % 9 != 0) {
      error = "Surface data does not contain a multiple of 9 numeric values: " +
              currentLabel + " in " + path;
      return false;
    }
    SurfaceData surface;
    surface.group = groupLabel;
    surface.label = currentLabel.empty() ? fallbackLabel : currentLabel;
    surface.originalLabel = surface.label;
    surface.sourceFile = path;
    surface.data = BuildPolyData(currentValues);
    ApplyTransform(surface.data, scale, translate);
    surfaces.push_back(surface);
    currentValues.clear();
    return true;
  };

  while (file >> token) {
    float value = 0.0f;
    if (TryParseFloat(token, value)) {
      currentValues.push_back(value);
      sawNumeric = true;
      continue;
    }

    if (!flushSurface()) {
      return false;
    }
    currentLabel = token;
  }

  if (!flushSurface()) {
    return false;
  }

  if (!sawNumeric) {
    error = "Surface file does not contain any numeric data: " + path;
    return false;
  }

  return true;
}
