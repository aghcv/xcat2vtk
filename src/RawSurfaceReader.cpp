#include "RawSurfaceReader.h"

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
} // namespace

bool RawSurfaceReader::ReadAll(
  const std::string &path,
  const std::string &groupLabel,
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
    surface.data = BuildPolyData(currentValues);
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
