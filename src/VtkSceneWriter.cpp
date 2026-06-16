#include "VtkSceneWriter.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

#include <vtkCompositeDataSet.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkDataSetAttributes.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkInformation.h>
#include <vtkIntArray.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPolyData.h>
#include <vtkStringArray.h>
#include <vtkXMLImageDataWriter.h>
#include <vtkXMLMultiBlockDataWriter.h>
#include <vtkXMLPolyDataWriter.h>

namespace {
void AddStringField(vtkDataObject *object,
                    const std::string &name,
                    const std::string &value) {
  if (!object) {
    return;
  }
  vtkSmartPointer<vtkStringArray> array = vtkSmartPointer<vtkStringArray>::New();
  array->SetName(name.c_str());
  array->InsertNextValue(value);
  object->GetFieldData()->AddArray(array);
}

void AddRepeatedStringArray(vtkDataSetAttributes *attributes,
                            const std::string &name,
                            const std::string &value,
                            vtkIdType tupleCount) {
  if (!attributes) {
    return;
  }
  vtkSmartPointer<vtkStringArray> array = vtkSmartPointer<vtkStringArray>::New();
  array->SetName(name.c_str());
  array->SetNumberOfValues(tupleCount);
  for (vtkIdType i = 0; i < tupleCount; ++i) {
    array->SetValue(i, value);
  }
  attributes->RemoveArray(name.c_str());
  attributes->AddArray(array);
}

void AddRepeatedIntArray(vtkDataSetAttributes *attributes,
                         const std::string &name,
                         int value,
                         vtkIdType tupleCount) {
  if (!attributes) {
    return;
  }
  vtkSmartPointer<vtkIntArray> array = vtkSmartPointer<vtkIntArray>::New();
  array->SetName(name.c_str());
  array->SetNumberOfValues(tupleCount);
  for (vtkIdType i = 0; i < tupleCount; ++i) {
    array->SetValue(i, value);
  }
  attributes->RemoveArray(name.c_str());
  attributes->AddArray(array);
}

void AddSurfaceIdentityArrays(vtkDataObject *object,
                              const AnatomyClassification &classification) {
  vtkPolyData *polyData = vtkPolyData::SafeDownCast(object);
  if (!polyData) {
    return;
  }
  vtkCellData *cellData = polyData->GetCellData();
  const vtkIdType cellCount = polyData->GetNumberOfCells();
  AddRepeatedStringArray(
      cellData, "OriginalBlockName", classification.originalName, cellCount);
  AddRepeatedStringArray(
      cellData, "NormalizedAnatomyName", classification.normalizedName, cellCount);
  AddRepeatedStringArray(
      cellData, "CanonicalAnatomyName", classification.canonicalName, cellCount);
  AddRepeatedIntArray(
      cellData, "OriginalBlockIndex", classification.originalBlockIndex, cellCount);
}

void AddAnatomyMetadata(vtkDataObject *object,
                        const AnatomyClassification &classification) {
  AddStringField(object, "OriginalBlockName", classification.originalName);
  AddStringField(object, "OriginalBlockIndex",
                 std::to_string(classification.originalBlockIndex));
  AddStringField(object, "NormalizedAnatomyName", classification.normalizedName);
  AddStringField(object, "CanonicalAnatomyName", classification.canonicalName);
  AddStringField(object, "CanonicalAnatomyIdentifier",
                 classification.canonicalIdentifier);
  AddStringField(object, "AnatomicalSystem", classification.system);
  AddStringField(object, "AnatomicalSubsystem", classification.subsystem);
  AddStringField(object, "AnatomicalRegion", classification.anatomicalRegion);
  AddStringField(object, "StructureType", classification.structureType);
  AddStringField(object, "Laterality", classification.laterality);
  AddStringField(object, "PieceNumber", classification.pieceNumber);
  AddStringField(object, "TemporalPhase", classification.temporalPhase);
  AddStringField(object, "ClassificationConfidence", classification.confidence);
  AddStringField(object, "ClassificationRule", classification.classificationSource);
  AddStringField(object, "HierarchyPath", classification.hierarchyPath);
  AddStringField(object, "SecondarySystems", classification.secondarySystems);
  AddStringField(object, "SourceFile", classification.sourceFile);
  AddSurfaceIdentityArrays(object, classification);
}

AnatomyClassification MakeFieldClassification(const std::string &name,
                                               int index) {
  AnatomyClassification classification;
  classification.originalName = name;
  classification.normalizedName = NormalizeAnatomyLabelText(name);
  classification.canonicalName = classification.normalizedName;
  classification.system = "fields";
  classification.subsystem = classification.normalizedName;
  classification.anatomicalRegion = "whole_body";
  classification.structureType = "other";
  classification.laterality = "unspecified";
  classification.temporalPhase = "static";
  classification.confidence = "exact";
  classification.classificationSource = "field:" + classification.normalizedName;
  classification.hierarchyPath = "00_Fields";
  classification.displayName = name;
  classification.canonicalIdentifier =
      "fields__" + classification.normalizedName +
      "__whole_body__" + classification.normalizedName + "__unspecified__000";
  classification.originalBlockIndex = index;
  classification.unclassified = false;
  return classification;
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

void SetBlockName(vtkMultiBlockDataSet *dataSet,
                  unsigned int index,
                  const std::string &name) {
  dataSet->GetMetaData(index)->Set(vtkCompositeDataSet::NAME(), name.c_str());
}

vtkSmartPointer<vtkMultiBlockDataSet> FindChildGroup(
    vtkMultiBlockDataSet *parent,
    const std::string &name) {
  const unsigned int count = parent->GetNumberOfBlocks();
  for (unsigned int i = 0; i < count; ++i) {
    vtkInformation *metadata = parent->GetMetaData(i);
    if (!metadata || !metadata->Has(vtkCompositeDataSet::NAME())) {
      continue;
    }
    const char *blockName = metadata->Get(vtkCompositeDataSet::NAME());
    if (!blockName || name != blockName) {
      continue;
    }
    vtkMultiBlockDataSet *child =
        vtkMultiBlockDataSet::SafeDownCast(parent->GetBlock(i));
    if (child) {
      return child;
    }
  }
  return nullptr;
}

vtkSmartPointer<vtkMultiBlockDataSet> EnsureChildGroup(
    vtkMultiBlockDataSet *parent,
    const std::string &name) {
  vtkSmartPointer<vtkMultiBlockDataSet> existing = FindChildGroup(parent, name);
  if (existing) {
    return existing;
  }
  vtkSmartPointer<vtkMultiBlockDataSet> child =
      vtkSmartPointer<vtkMultiBlockDataSet>::New();
  const unsigned int index = parent->GetNumberOfBlocks();
  parent->SetBlock(index, child);
  SetBlockName(parent, index, name);
  return child;
}

void InsertDatasetAtPath(vtkMultiBlockDataSet *root,
                         const std::vector<std::string> &path,
                         vtkDataObject *data,
                         const std::string &leafName) {
  vtkMultiBlockDataSet *current = root;
  for (const std::string &segment : path) {
    current = EnsureChildGroup(current, segment);
  }
  const unsigned int index = current->GetNumberOfBlocks();
  current->SetBlock(index, data);
  SetBlockName(current, index, leafName);
}

std::string UniqueName(const std::string &base,
                       std::map<std::string, int> &counts) {
  const int count = ++counts[base];
  return count == 1 ? base : base + "_" + std::to_string(count);
}

std::string SourceLabelForClassification(const SurfaceData &surface) {
  if (!surface.originalLabel.empty()) {
    return surface.originalLabel;
  }
  if (!surface.label.empty()) {
    return surface.label;
  }
  return "surface";
}

bool BuildAnatomyClassifications(
    const std::vector<SurfaceData> &surfaces,
    const VtkSceneWriteOptions &options,
    std::vector<AnatomyClassification> &classifications,
    std::string &error) {
  if (!options.anatomyConfig) {
    error = "Anatomy hierarchy requested without an anatomy config.";
    return false;
  }

  AnatomyClassifier classifier(*options.anatomyConfig);
  classifications.reserve(surfaces.size());
  bool sawUnclassified = false;
  for (size_t i = 0; i < surfaces.size(); ++i) {
    const SurfaceData &surface = surfaces[i];
    const int originalIndex =
        surface.originalIndex >= 0 ? surface.originalIndex : static_cast<int>(i);
    AnatomyClassification classification = classifier.Classify(
        SourceLabelForClassification(surface),
        originalIndex,
        surface.sourceFile);
    if (options.anatomyOverrides &&
        ApplyAnatomyReportOverride(*options.anatomyConfig,
                                   *options.anatomyOverrides,
                                   classification)) {
      if (options.anatomyOverrideApplyCount) {
        ++(*options.anatomyOverrideApplyCount);
      }
    }
    classifications.push_back(classification);
    if (classification.unclassified) {
      sawUnclassified = true;
      std::cerr << "Warning: unclassified anatomy label: "
                << classification.originalName << "\n";
    }
  }

  std::map<std::string, int> displayCounts;
  std::map<std::string, int> canonicalCounts;
  for (AnatomyClassification &classification : classifications) {
    classification.displayName = UniqueName(classification.displayName, displayCounts);
    classification.canonicalIdentifier =
        UniqueName(classification.canonicalIdentifier, canonicalCounts);
  }

  if (options.strictAnatomy && sawUnclassified) {
    error = "Strict anatomy mode failed because at least one surface label is unclassified.";
    return false;
  }
  return true;
}

bool WriteHierarchicalScene(const std::filesystem::path &outDir,
                            vtkSmartPointer<vtkImageData> activity,
                            vtkSmartPointer<vtkImageData> attenuation,
                            const std::vector<SurfaceData> &surfaces,
                            const VtkSceneWriteOptions &options,
                            vtkSmartPointer<vtkMultiBlockDataSet> &multiBlock,
                            std::string &error) {
  std::vector<AnatomyClassification> classifications;
  if (!BuildAnatomyClassifications(surfaces, options, classifications, error)) {
    return false;
  }

  multiBlock = vtkSmartPointer<vtkMultiBlockDataSet>::New();

  int fieldIndex = -2;
  if (activity) {
    AnatomyClassification classification =
        MakeFieldClassification("Activity", fieldIndex++);
    AddAnatomyMetadata(activity, classification);
    const std::filesystem::path vtiPath = outDir / "activity.vti";
    if (!VtkSceneWriter::WriteImageData(vtiPath.string(), activity, error)) {
      return false;
    }
    InsertDatasetAtPath(multiBlock,
                        SplitHierarchyPath(classification.hierarchyPath),
                        activity,
                        "Activity");
  }

  if (attenuation) {
    AnatomyClassification classification =
        MakeFieldClassification("Attenuation", fieldIndex++);
    AddAnatomyMetadata(attenuation, classification);
    const std::filesystem::path vtiPath = outDir / "attenuation.vti";
    if (!VtkSceneWriter::WriteImageData(vtiPath.string(), attenuation, error)) {
      return false;
    }
    InsertDatasetAtPath(multiBlock,
                        SplitHierarchyPath(classification.hierarchyPath),
                        attenuation,
                        "Attenuation");
  }

  std::vector<size_t> reportOrder(classifications.size());
  for (size_t i = 0; i < reportOrder.size(); ++i) {
    reportOrder[i] = i;
  }
  std::sort(reportOrder.begin(), reportOrder.end(), [&](size_t left, size_t right) {
    return classifications[left].originalBlockIndex <
           classifications[right].originalBlockIndex;
  });
  for (const size_t i : reportOrder) {
    if (surfaces[i].placeholder) {
      continue;
    }
    if (options.anatomySummary) {
      options.anatomySummary->Add(classifications[i]);
    }
    if (options.anatomyReport) {
      WriteAnatomyReportRow(*options.anatomyReport, classifications[i]);
    }
  }

  std::vector<size_t> order(classifications.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](size_t left, size_t right) {
    const AnatomyClassification &a = classifications[left];
    const AnatomyClassification &b = classifications[right];
    if (a.hierarchyPath != b.hierarchyPath) {
      return a.hierarchyPath < b.hierarchyPath;
    }
    if (a.displayName != b.displayName) {
      return a.displayName < b.displayName;
    }
    return a.originalBlockIndex < b.originalBlockIndex;
  });

  for (const size_t surfaceIndex : order) {
    const SurfaceData &surface = surfaces[surfaceIndex];
    if (!surface.data) {
      continue;
    }
    const AnatomyClassification &classification = classifications[surfaceIndex];
    AddAnatomyMetadata(surface.data, classification);

    const std::filesystem::path vtpPath =
        outDir / (classification.displayName + ".vtp");
    if (!WritePolyData(vtpPath.string(),
                       surface.data,
                       classification.displayName,
                       surface.group,
                       error)) {
      return false;
    }

    InsertDatasetAtPath(multiBlock,
                        SplitHierarchyPath(classification.hierarchyPath),
                        surface.data,
                        classification.displayName);
  }

  return true;
}
} // namespace

bool VtkSceneWriter::WriteImageData(const std::string &path,
                                    vtkSmartPointer<vtkImageData> image,
                                    std::string &error) {
  const std::filesystem::path imagePath(path);
  const std::filesystem::path parent = imagePath.parent_path();
  if (!parent.empty()) {
    std::error_code fsError;
    std::filesystem::create_directories(parent, fsError);
    if (fsError) {
      error = "Failed to create output directory: " + parent.string();
      return false;
    }
  }

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

bool VtkSceneWriter::WriteScene(
    const std::string &outputDir,
    const std::string &sceneFileName,
    vtkSmartPointer<vtkImageData> activity,
    vtkSmartPointer<vtkImageData> attenuation,
    const std::vector<SurfaceData> &surfaces,
    std::string &error,
    const VtkSceneWriteOptions &options) {
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

  vtkSmartPointer<vtkMultiBlockDataSet> multiBlock;

  if (options.anatomyHierarchy) {
    if (!WriteHierarchicalScene(outDir,
                                activity,
                                attenuation,
                                surfaces,
                                options,
                                multiBlock,
                                error)) {
      return false;
    }
  } else {
    multiBlock = vtkSmartPointer<vtkMultiBlockDataSet>::New();

    unsigned int blockIndex = 0;

    if (activity) {
      const std::filesystem::path vtiPath = outDir / "activity.vti";
      if (!WriteImageData(vtiPath.string(), activity, error)) {
        return false;
      }
      multiBlock->SetBlock(blockIndex, activity);
      multiBlock->GetMetaData(blockIndex)->Set(vtkCompositeDataSet::NAME(), "activity");
      ++blockIndex;
    }

    if (attenuation) {
      const std::filesystem::path vtiPath = outDir / "attenuation.vti";
      if (!WriteImageData(vtiPath.string(), attenuation, error)) {
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
