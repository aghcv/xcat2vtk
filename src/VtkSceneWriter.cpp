#include "VtkSceneWriter.h"

#include <filesystem>
#include <map>

#include <vtkCompositeDataSet.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkInformation.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPolyData.h>
#include <vtkStringArray.h>
#include <vtkXMLImageDataWriter.h>
#include <vtkXMLMultiBlockDataWriter.h>
#include <vtkXMLPolyDataWriter.h>

namespace {
bool WriteImage(const std::string &path,
                vtkSmartPointer<vtkImageData> image,
                std::string &error) {
  vtkSmartPointer<vtkXMLImageDataWriter> writer =
      vtkSmartPointer<vtkXMLImageDataWriter>::New();
  writer->SetFileName(path.c_str());
  writer->SetInputData(image);
  if (writer->Write() == 0) {
    error = "Failed to write image data: " + path;
    return false;
  }
  return true;
}

bool WritePolyData(const std::string &path,
                   vtkSmartPointer<vtkPolyData> polyData,
                   const std::string &label,
                   const std::string &group,
                   std::string &error) {
  if (!label.empty()) {
    vtkSmartPointer<vtkStringArray> labelArray = vtkSmartPointer<vtkStringArray>::New();
    labelArray->SetName("label");
    labelArray->InsertNextValue(label);
    polyData->GetFieldData()->AddArray(labelArray);
  }
  if (!group.empty()) {
    vtkSmartPointer<vtkStringArray> groupArray = vtkSmartPointer<vtkStringArray>::New();
    groupArray->SetName("group");
    groupArray->InsertNextValue(group);
    polyData->GetFieldData()->AddArray(groupArray);
  }
  vtkSmartPointer<vtkXMLPolyDataWriter> writer =
      vtkSmartPointer<vtkXMLPolyDataWriter>::New();
  writer->SetFileName(path.c_str());
  writer->SetInputData(polyData);
  if (writer->Write() == 0) {
    error = "Failed to write surface data: " + path;
    return false;
  }
  return true;
}
} // namespace

bool VtkSceneWriter::WriteScene(
    const std::string &outputDir,
    const std::string &sceneFileName,
    vtkSmartPointer<vtkImageData> activity,
    vtkSmartPointer<vtkImageData> attenuation,
  const std::vector<SurfaceData> &surfaces,
    std::string &error) {
  std::filesystem::path outDir;
  if (outputDir.empty()) {
    outDir = std::filesystem::current_path();
  } else {
    outDir = outputDir;
  }
  std::error_code fsError;
  std::filesystem::create_directories(outDir, fsError);
  if (fsError) {
    error = "Failed to create output directory: " + outDir.string();
    return false;
  }

  vtkSmartPointer<vtkMultiBlockDataSet> multiBlock =
      vtkSmartPointer<vtkMultiBlockDataSet>::New();

  unsigned int blockIndex = 0;

  if (activity) {
    const std::filesystem::path vtiPath = outDir / "activity.vti";
    if (!WriteImage(vtiPath.string(), activity, error)) {
      return false;
    }
    multiBlock->SetBlock(blockIndex, activity);
    multiBlock->GetMetaData(blockIndex)->Set(vtkCompositeDataSet::NAME(), "activity");
    ++blockIndex;
  }

  if (attenuation) {
    const std::filesystem::path vtiPath = outDir / "attenuation.vti";
    if (!WriteImage(vtiPath.string(), attenuation, error)) {
      return false;
    }
    multiBlock->SetBlock(blockIndex, attenuation);
    multiBlock->GetMetaData(blockIndex)->Set(vtkCompositeDataSet::NAME(), "attenuation");
    ++blockIndex;
  }

  std::map<std::string, int> labelCounts;
  for (const auto &surface : surfaces) {
    if (!surface.data) {
      continue;
    }
    const std::string baseLabel = surface.label.empty() ? "surface" : surface.label;
    const int count = ++labelCounts[baseLabel];
    const std::string label = (count == 1) ? baseLabel : baseLabel + "_" + std::to_string(count);
    const std::filesystem::path vtpPath = outDir / (label + ".vtp");
    if (!WritePolyData(vtpPath.string(), surface.data, label, surface.group, error)) {
      return false;
    }
    multiBlock->SetBlock(blockIndex, surface.data);
    multiBlock->GetMetaData(blockIndex)->Set(vtkCompositeDataSet::NAME(), label.c_str());
    ++blockIndex;
  }

  vtkSmartPointer<vtkXMLMultiBlockDataWriter> writer =
      vtkSmartPointer<vtkXMLMultiBlockDataWriter>::New();
  const std::filesystem::path scenePath = outDir / sceneFileName;
  writer->SetFileName(scenePath.string().c_str());
  writer->SetInputData(multiBlock);
  if (writer->Write() == 0) {
    error = "Failed to write multi-block scene: " + scenePath.string();
    return false;
  }

  return true;
}
