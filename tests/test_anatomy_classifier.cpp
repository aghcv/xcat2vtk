#include "AnatomyClassifier.h"
#include "VtkSceneWriter.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <vtkCellArray.h>
#include <vtkCompositeDataSet.h>
#include <vtkDataObject.h>
#include <vtkFieldData.h>
#include <vtkInformation.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkXMLMultiBlockDataReader.h>

namespace {
void Require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

vtkSmartPointer<vtkPolyData> MakeTriangle() {
  vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
  points->InsertNextPoint(0.0, 0.0, 0.0);
  points->InsertNextPoint(1.0, 0.0, 0.0);
  points->InsertNextPoint(0.0, 1.0, 0.0);

  vtkSmartPointer<vtkCellArray> cells = vtkSmartPointer<vtkCellArray>::New();
  vtkIdType ids[3] = {0, 1, 2};
  cells->InsertNextCell(3, ids);

  vtkSmartPointer<vtkPolyData> polyData = vtkSmartPointer<vtkPolyData>::New();
  polyData->SetPoints(points);
  polyData->SetPolys(cells);
  return polyData;
}

std::string BlockName(vtkMultiBlockDataSet *parent, unsigned int index) {
  vtkInformation *metadata = parent->GetMetaData(index);
  if (!metadata || !metadata->Has(vtkCompositeDataSet::NAME())) {
    return std::string();
  }
  const char *name = metadata->Get(vtkCompositeDataSet::NAME());
  return name ? std::string(name) : std::string();
}

vtkDataObject *FindPath(vtkMultiBlockDataSet *root,
                        const std::vector<std::string> &path) {
  vtkMultiBlockDataSet *current = root;
  for (size_t depth = 0; depth < path.size(); ++depth) {
    bool found = false;
    for (unsigned int i = 0; i < current->GetNumberOfBlocks(); ++i) {
      if (BlockName(current, i) != path[depth]) {
        continue;
      }
      vtkDataObject *object = current->GetBlock(i);
      if (depth + 1 == path.size()) {
        return object;
      }
      current = vtkMultiBlockDataSet::SafeDownCast(object);
      found = current != nullptr;
      break;
    }
    if (!found) {
      return nullptr;
    }
  }
  return current;
}

void CollectLeaves(vtkMultiBlockDataSet *root,
                   std::vector<vtkDataObject *> &leaves) {
  for (unsigned int i = 0; i < root->GetNumberOfBlocks(); ++i) {
    vtkDataObject *object = root->GetBlock(i);
    if (!object) {
      continue;
    }
    vtkMultiBlockDataSet *child = vtkMultiBlockDataSet::SafeDownCast(object);
    if (child) {
      CollectLeaves(child, leaves);
    } else {
      leaves.push_back(object);
    }
  }
}

std::string FieldString(vtkDataObject *object, const std::string &name) {
  vtkAbstractArray *array = object->GetFieldData()->GetAbstractArray(name.c_str());
  vtkStringArray *strings = vtkStringArray::SafeDownCast(array);
  if (!strings || strings->GetNumberOfValues() == 0) {
    return std::string();
  }
  return strings->GetValue(0);
}

SurfaceData Surface(const std::string &label) {
  SurfaceData surface;
  surface.label = label;
  surface.originalLabel = label;
  surface.sourceFile = "synthetic.raw";
  surface.data = MakeTriangle();
  return surface;
}

void TestClassificationExamples() {
  AnatomyConfig config = DefaultAnatomyConfig();
  AnatomyClassifier classifier(config);

  AnatomyClassification c = classifier.Classify("l_pulmonary_artery");
  Require(c.normalizedName == "pulmonary_artery", "left pulmonary artery normalization");
  Require(c.laterality == "left", "left pulmonary artery laterality");
  Require(c.hierarchyPath == "01_Cardiovascular/Pulmonary_Circulation/Arteries",
          "pulmonary artery hierarchy");
  Require(c.structureType == "artery", "pulmonary artery type");

  c = classifier.Classify("r_pulmonary_vein");
  Require(c.laterality == "right", "right pulmonary vein laterality");
  Require(c.hierarchyPath == "01_Cardiovascular/Pulmonary_Circulation/Veins",
          "pulmonary vein hierarchy");

  c = classifier.Classify("lkidney");
  Require(c.normalizedName == "kidney", "lkidney alias normalization");
  Require(c.laterality == "left", "lkidney laterality");
  Require(c.system == "urinary", "kidney system");
  Require(c.confidence == "exact", "kidney alias confidence");

  c = classifier.Classify("rkidney");
  Require(c.laterality == "right", "rkidney laterality");

  c = classifier.Classify("l_medulla");
  Require(c.laterality == "left", "l_medulla laterality");
  Require(c.hierarchyPath == "02_Nervous/Brainstem", "medulla hierarchy");

  c = classifier.Classify("dias_lv");
  Require(c.temporalPhase == "diastole", "dias_lv temporal phase");
  Require(c.canonicalName == "left_ventricle", "dias_lv canonical name");
  Require(c.hierarchyPath == "01_Cardiovascular/Heart_and_Pericardium",
          "dias_lv hierarchy");

  c = classifier.Classify("dias_lad1");
  Require(c.temporalPhase == "diastole", "dias_lad1 temporal phase");
  Require(c.pieceNumber == "1", "dias_lad1 piece number: " + c.pieceNumber);
  Require(c.canonicalName == "left_anterior_descending_coronary_artery",
          "dias_lad1 canonical name");
  Require(c.hierarchyPath ==
              "01_Cardiovascular/Coronary_Circulation/Coronary_Arteries",
          "dias_lad1 coronary hierarchy");

  c = classifier.Classify("bronchi_17");
  Require(c.pieceNumber == "17", "bronchi piece extraction");
  Require(c.displayName == "bronchus_017", "bronchi fragment display name");
  Require(c.hierarchyPath == "03_Respiratory/Tracheobronchial_Tree/Bronchi",
          "bronchi hierarchy");

  c = classifier.Classify("left_femur");
  Require(c.laterality == "left", "left_femur laterality");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Lower_Limb",
          "left_femur hierarchy");

  c = classifier.Classify("r_femur");
  Require(c.laterality == "right", "r_femur laterality");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Lower_Limb",
          "r_femur hierarchy");

  c = classifier.Classify("ascending_aorta");
  Require(c.hierarchyPath == "01_Cardiovascular/Great_Vessels",
          "aorta hierarchy");

  c = classifier.Classify("inferior_vena_cava");
  Require(c.hierarchyPath == "01_Cardiovascular/Great_Vessels",
          "vena cava hierarchy");

  c = classifier.Classify("portal_vein");
  Require(c.hierarchyPath == "01_Cardiovascular/Portal_and_Hepatic_Venous_System",
          "portal vein hierarchy");

  c = classifier.Classify("spinal_cord");
  Require(c.hierarchyPath == "02_Nervous/Spinal_Cord",
          "spinal cord hierarchy");

  c = classifier.Classify("left_pulmonary_arts");
  Require(c.laterality == "left", "left_pulmonary_arts laterality");
  Require(c.canonicalName == "pulmonary_artery",
          "left_pulmonary_arts canonical name");
  Require(c.hierarchyPath == "01_Cardiovascular/Pulmonary_Circulation/Arteries",
          "left_pulmonary_arts hierarchy");

  c = classifier.Classify("lateral_sinus_right");
  Require(c.laterality == "right", "lateral_sinus_right laterality");
  Require(c.hierarchyPath ==
              "01_Cardiovascular/Cerebrovascular/Veins_and_Dural_Sinuses",
          "lateral_sinus_right hierarchy");

  c = classifier.Classify("middle_cerebral_M2_left");
  Require(c.laterality == "left", "middle_cerebral_M2_left laterality");
  Require(c.pieceNumber == "2", "middle_cerebral_M2_left piece");
  Require(c.hierarchyPath == "01_Cardiovascular/Cerebrovascular/Arteries",
          "middle cerebral artery hierarchy");

  c = classifier.Classify("lateral_occipital_left");
  Require(c.laterality == "left", "lateral_occipital_left laterality");
  Require(c.hierarchyPath == "02_Nervous/Brain",
          "lateral_occipital_left hierarchy");

  c = classifier.Classify("Occipital_right1");
  Require(c.laterality == "right", "Occipital_right1 laterality after piece");
  Require(c.pieceNumber == "1", "Occipital_right1 piece number");
  Require(c.hierarchyPath == "02_Nervous/Brain",
          "Occipital_right1 hierarchy");

  c = classifier.Classify("Mamillary_bodies");
  Require(c.hierarchyPath == "02_Nervous/Brain",
          "Mamillary_bodies hierarchy");

  c = classifier.Classify("Substantia_nigra");
  Require(c.hierarchyPath == "02_Nervous/Brainstem",
          "Substantia_nigra hierarchy");

  c = classifier.Classify("Pineal_gland");
  Require(c.system == "endocrine", "Pineal_gland endocrine system");
  Require(c.secondarySystems == "nervous", "Pineal_gland secondary nervous");

  c = classifier.Classify("left_dia");
  Require(c.laterality == "left", "left_dia laterality");
  Require(c.canonicalName == "diaphragm", "left_dia canonical name");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeletal_Muscle",
          "left_dia hierarchy");
  Require(c.secondarySystems == "respiratory", "left_dia secondary respiratory");

  c = classifier.Classify("llung");
  Require(c.laterality == "left", "llung laterality");
  Require(c.hierarchyPath == "03_Respiratory/Lungs", "llung hierarchy");

  c = classifier.Classify("sinus");
  Require(c.hierarchyPath == "03_Respiratory/Upper_Airway/Paranasal_Sinuses",
          "bare sinus hierarchy");

  c = classifier.Classify("small_intest");
  Require(c.hierarchyPath == "04_Digestive/Small_Intestine",
          "small_intest hierarchy");

  c = classifier.Classify("trans_large_int");
  Require(c.hierarchyPath == "04_Digestive/Large_Intestine",
          "trans_large_int hierarchy");

  c = classifier.Classify("r_epididymus");
  Require(c.laterality == "right", "r_epididymus laterality");
  Require(c.hierarchyPath == "06_Reproductive/Male",
          "r_epididymus hierarchy");

  c = classifier.Classify("r_vas_def");
  Require(c.hierarchyPath == "06_Reproductive/Male",
          "r_vas_def hierarchy");

  c = classifier.Classify("r_parotid");
  Require(c.laterality == "right", "r_parotid laterality");
  Require(c.hierarchyPath == "04_Digestive/Oral_Cavity/Salivary_Glands",
          "r_parotid hierarchy");

  c = classifier.Classify("r_sclera");
  Require(c.hierarchyPath == "08_Sensory/Visual_System",
          "r_sclera hierarchy");

  c = classifier.Classify("phalanx1512");
  Require(c.pieceNumber == "1512", "phalanx1512 piece number");
  Require(c.canonicalName == "phalanx", "phalanx1512 canonical name");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Phalanges",
          "phalanx1512 hierarchy");

  c = classifier.Classify("r_capitate");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Upper_Limb",
          "r_capitate hierarchy");

  c = classifier.Classify("r_talus");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Lower_Limb",
          "r_talus hierarchy");

  c = classifier.Classify("lfoot_musc");
  Require(c.laterality == "left", "lfoot_musc laterality");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeletal_Muscle/Lower_Limb",
          "lfoot_musc hierarchy");

  c = classifier.Classify("leg_right");
  Require(c.laterality == "right", "leg_right laterality");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeletal_Muscle/Lower_Limb",
          "leg_right hierarchy");

  c = classifier.Classify("musc1071");
  Require(c.canonicalName == "skeletal_muscle", "musc1071 canonical name");
  Require(c.pieceNumber == "1071", "musc1071 piece number");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeletal_Muscle",
          "musc1071 hierarchy");

  c = classifier.Classify("thumb_l");
  Require(c.laterality == "left", "thumb_l laterality");
  Require(c.hierarchyPath == "09_Musculoskeletal/Skeleton/Upper_Limb",
          "thumb_l hierarchy");

  c = classifier.Classify("vocal_folds");
  Require(c.hierarchyPath == "03_Respiratory/Upper_Airway/Larynx",
          "vocal_folds hierarchy");

  c = classifier.Classify("unknown_structure_xyz");
  Require(c.unclassified, "unknown label fallback");
  Require(c.hierarchyPath == "99_Unclassified", "unknown hierarchy");
}

void TestConfigPriorityAndReport() {
  AnatomyConfig config = DefaultAnatomyConfig();
  AnatomyClassifier classifier(config);
  AnatomyClassification pulmonary = classifier.Classify("pulmonary_artery_17");
  Require(pulmonary.hierarchyPath ==
              "01_Cardiovascular/Pulmonary_Circulation/Arteries",
          "specific pulmonary artery rule outranks generic artery");

  std::stringstream report;
  Require(WriteAnatomyReportHeader(report), "report header write");
  Require(WriteAnatomyReportRow(report, pulmonary), "report row write");
  const std::string csv = report.str();
  Require(csv.find("original_block_index,original_name") != std::string::npos,
          "report header present");
  Require(csv.find("pulmonary_artery_17") != std::string::npos,
          "report row present");
}

void TestHierarchicalWriter() {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "xcat2vtk_anatomy_test";
  std::filesystem::remove_all(dir);

  std::vector<SurfaceData> surfaces = {
      Surface("l_pulmonary_artery"),
      Surface("bronchi_17"),
      Surface("unknown_structure_xyz"),
  };

  AnatomyConfig config = DefaultAnatomyConfig();
  AnatomySummary summary;
  std::stringstream report;
  WriteAnatomyReportHeader(report);

  VtkSceneWriteOptions options;
  options.anatomyHierarchy = true;
  options.anatomyConfig = &config;
  options.anatomySummary = &summary;
  options.anatomyReport = &report;

  std::string error;
  Require(VtkSceneWriter::WriteScene(dir.string(),
                                     "scene.vtm",
                                     nullptr,
                                     nullptr,
                                     surfaces,
                                     error,
                                     options),
          "hierarchical scene write: " + error);
  Require(summary.inputBlocks == 3, "summary input block count");
  Require(summary.classifiedBlocks == 2, "summary classified count");
  Require(summary.unclassifiedBlocks == 1, "summary unclassified count");

  vtkSmartPointer<vtkXMLMultiBlockDataReader> reader =
      vtkSmartPointer<vtkXMLMultiBlockDataReader>::New();
  reader->SetFileName((dir / "scene.vtm").string().c_str());
  reader->Update();
  vtkMultiBlockDataSet *root =
      vtkMultiBlockDataSet::SafeDownCast(reader->GetOutput());
  Require(root != nullptr, "read hierarchical scene");

  vtkDataObject *pulmonary = FindPath(root,
                                      {"01_Cardiovascular",
                                       "Pulmonary_Circulation",
                                       "Arteries",
                                       "left_pulmonary_artery"});
  Require(pulmonary != nullptr, "pulmonary artery path exists");
  Require(FieldString(pulmonary, "OriginalBlockName") == "l_pulmonary_artery",
          "pulmonary metadata original name");
  Require(FieldString(pulmonary, "AnatomicalSystem") == "cardiovascular",
          "pulmonary metadata system");

  vtkDataObject *bronchus = FindPath(root,
                                     {"03_Respiratory",
                                      "Tracheobronchial_Tree",
                                      "Bronchi",
                                      "bronchus_017"});
  Require(bronchus != nullptr, "bronchus path exists");
  Require(FieldString(bronchus, "PieceNumber") == "17",
          "bronchus metadata piece number");

  vtkDataObject *unknown =
      FindPath(root, {"99_Unclassified", "unknown_structure_xyz"});
  Require(unknown != nullptr, "unclassified path exists");

  std::vector<vtkDataObject *> leaves;
  CollectLeaves(root, leaves);
  Require(leaves.size() == 3, "preserve all leaves exactly once");

  std::set<std::string> canonicalIds;
  for (vtkDataObject *leaf : leaves) {
    const std::string id = FieldString(leaf, "CanonicalAnatomyIdentifier");
    Require(!id.empty(), "canonical id metadata present");
    canonicalIds.insert(id);
  }
  Require(canonicalIds.size() == leaves.size(), "canonical ids unique");

  VtkSceneWriteOptions strictOptions = options;
  strictOptions.strictAnatomy = true;
  std::string strictError;
  Require(!VtkSceneWriter::WriteScene((dir / "strict").string(),
                                      "scene.vtm",
                                      nullptr,
                                      nullptr,
                                      surfaces,
                                      strictError,
                                      strictOptions),
          "strict mode fails with unclassified label");

  std::filesystem::remove_all(dir);
}
} // namespace

int main() {
  TestClassificationExamples();
  TestConfigPriorityAndReport();
  TestHierarchicalWriter();
  std::cout << "anatomy tests passed\n";
  return 0;
}
