#pragma once

#include <string>
#include <vector>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include "SurfaceTypes.h"

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
      std::string &error);
};
