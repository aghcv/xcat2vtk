#pragma once

#include <string>
#include <vector>
#include <iosfwd>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include "AnatomyClassifier.h"
#include "SurfaceTypes.h"

struct VtkSceneWriteOptions {
  bool anatomyHierarchy = false;
  bool strictAnatomy = false;
  const AnatomyConfig *anatomyConfig = nullptr;
  const AnatomyReportOverrides *anatomyOverrides = nullptr;
  std::ostream *anatomyReport = nullptr;
  AnatomySummary *anatomySummary = nullptr;
  size_t *anatomyOverrideApplyCount = nullptr;
};

class VtkSceneWriter {
public:
  static bool WriteImageData(
      const std::string &path,
      vtkSmartPointer<vtkImageData> image,
      std::string &error);

  static bool WriteScene(
      const std::string &outputDir,
      const std::string &sceneFileName,
      vtkSmartPointer<vtkImageData> activity,
      vtkSmartPointer<vtkImageData> attenuation,
      const std::vector<SurfaceData> &surfaces,
      std::string &error,
      const VtkSceneWriteOptions &options = VtkSceneWriteOptions{});
};
