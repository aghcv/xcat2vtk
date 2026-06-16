#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "BinVolumeReader.h"
#include "RawSurfaceReader.h"
#include "VtkSceneWriter.h"

#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkStringArray.h>

namespace {
struct Options {
  std::string id;
  std::string inputDir;
  std::string activityPath;
  std::string attenuationPath;
  std::string surfacePath;
  std::string surfaceLabel = "surface";
  std::vector<std::pair<std::string, std::string>> surfaceFiles;
  std::string outputDir;
  int frame = 0;
  bool frameProvided = false;
  std::array<int, 3> dims{0, 0, 0};
  std::array<double, 3> spacing{1.0, 1.0, 1.0};
  std::array<double, 3> origin{0.0, 0.0, 0.0};
  std::array<double, 3> surfaceScale{1.0, 1.0, 1.0};
  std::array<double, 3> surfaceTranslate{0.0, 0.0, 0.0};
  double volumeBackgroundValue = 0.0;
  double volumeBackgroundEpsilon = 0.0;
  std::string volumeFitSource = "auto";
  bool dimsProvided = false;
  bool spacingProvided = false;
  bool originProvided = false;
  bool stableSurfaceOrder = true;
  bool fitVolumeToSurfaces = true;
  int sampleVtiBlockCount = 0;
  bool anatomyHierarchy = false;
  bool flatBlocks = false;
  bool strictAnatomy = false;
  std::string anatomyConfigPath;
  std::string anatomyReportPath;
  bool anatomyReport = false;
  std::string anatomyOverridesPath;
  std::string anatomyAtlasPath;
  bool anatomyAtlas = false;
  bool showHelp = false;
};

std::filesystem::path ResolveDefaultAnatomyAtlasPath() {
  const std::filesystem::path relativeAtlas = "config/anatomy_atlas.csv";
  std::vector<std::filesystem::path> candidates = {relativeAtlas};

  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    std::filesystem::path probe = cwd;
    for (int i = 0; i < 8; ++i) {
      candidates.push_back(probe / relativeAtlas);
      if (!probe.has_parent_path()) {
        break;
      }
      const std::filesystem::path parent = probe.parent_path();
      if (parent == probe) {
        break;
      }
      probe = parent;
    }
  }

  for (const auto &candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return relativeAtlas;
}

std::string ResolveAnatomyAtlasPath(const Options &options) {
  if (!options.anatomyAtlasPath.empty()) {
    return options.anatomyAtlasPath;
  }
  return ResolveDefaultAnatomyAtlasPath().string();
}

std::string ResolveAnatomyReportPath(const Options &options) {
  if (!options.anatomyReportPath.empty()) {
    return options.anatomyReportPath;
  }

  std::filesystem::path reportPath(options.outputDir);
  std::string fileName = "anatomy_report";
  if (!options.id.empty()) {
    fileName += "_" + options.id;
  }
  fileName += ".csv";
  reportPath /= fileName;
  return reportPath.string();
}

void PrintHelp() {
  std::cout
      << "xcat2vtk v0.1\n"
      << "Usage:\n"
      << "  xcat2vtk --output <dir> [options]\n\n"
      << "Options:\n"
      << "  --id <case_id>            Optional case id for auto-discovery\n"
      << "  --input <dir>             Input directory containing the case\n"
      << "  --frame <n>               Optional frame number when multiple exist\n"
      << "  --activity <path>         Optional activity .bin\n"
      << "  --attenuation <path>      Optional attenuation .bin\n"
      << "  --surface <path>          Optional surface .raw\n"
      << "  --surface-label <name>    Optional label for an unlabeled surface file\n"
      << "  --output <dir>            Required output directory\n"
      << "  --dims NX NY NZ           Required if a volume is provided\n"
      << "  --spacing DX DY DZ        Optional (default: 1 1 1)\n"
      << "  --origin OX OY OZ          Optional (default: 0 0 0)\n"
      << "  --surface-scale SX SY SZ   Optional (default: 1 1 1)\n"
      << "  --surface-translate TX TY TZ  Optional (default: 0 0 0)\n"
      << "  --no-stable-surface-order  Disable default global alphabetical surface block order\n"
      << "  --no-fit-volume-to-surfaces  Disable default volume fit to surface bounds\n"
      << "  --volume-background VALUE  Background value for occupied-voxel fitting (default: 0)\n"
      << "  --volume-background-epsilon EPS  Occupied-voxel tolerance (default: 0)\n"
      << "  --volume-fit-source <auto|attenuation|activity|union>\n"
      << "  --sample-vti-blocks N   Subdivide the non-background bounding box uniformly into\n"
      << "                          N blocks (e.g. 64 → 4x4x4 grid, 8 → 2x2x2).\n"
      << "                          Actual count = nx*ny*nz from best cube-like factorization.\n"
      << "  --anatomy-hierarchy     Write nested anatomical surface groups instead of flat blocks\n"
      << "  --anatomy-config PATH    Optional YAML anatomy rule/alias overlay\n"
      << "  --anatomy-report [PATH]  Write a CSV anatomy classification report\n"
      << "  --anatomy-overrides PATH Apply an edited anatomy_report.csv as batch corrections\n"
      << "  --anatomy-atlas [PATH]   Apply corrected rows from a reusable atlas CSV and\n"
      << "                           append new unclassified labels as needs_review rows\n"
      << "  --strict-anatomy         Fail when any surface label is unclassified\n"
      << "  --flat-blocks            Explicitly request legacy flat multiblock output\n"
      << "  --help                    Show this message\n";
}

bool ParseTriplet(int argc,
                  char **argv,
                  int &index,
                  std::array<int, 3> &out,
                  std::string &error) {
  if (index + 3 >= argc) {
    error = "Missing values for --dims";
    return false;
  }
  try {
    out[0] = std::stoi(argv[++index]);
    out[1] = std::stoi(argv[++index]);
    out[2] = std::stoi(argv[++index]);
  } catch (const std::exception &) {
    error = "Invalid integer for --dims";
    return false;
  }
  return true;
}

bool ParseTriplet(int argc,
                  char **argv,
                  int &index,
                  std::array<double, 3> &out,
                  const std::string &flag,
                  std::string &error) {
  if (index + 3 >= argc) {
    error = "Missing values for " + flag;
    return false;
  }
  try {
    out[0] = std::stod(argv[++index]);
    out[1] = std::stod(argv[++index]);
    out[2] = std::stod(argv[++index]);
  } catch (const std::exception &) {
    error = "Invalid number for " + flag;
    return false;
  }
  return true;
}

bool ParseArgs(int argc, char **argv, Options &options, std::string &error) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") {
      options.showHelp = true;
      return true;
    } else if (arg == "--id") {
      if (i + 1 >= argc) {
        error = "Missing value for --id";
        return false;
      }
      options.id = argv[++i];
    } else if (arg == "--input") {
      if (i + 1 >= argc) {
        error = "Missing value for --input";
        return false;
      }
      options.inputDir = argv[++i];
    } else if (arg == "--frame") {
      if (i + 1 >= argc) {
        error = "Missing value for --frame";
        return false;
      }
      try {
        options.frame = std::stoi(argv[++i]);
        options.frameProvided = true;
      } catch (const std::exception &) {
        error = "Invalid integer for --frame";
        return false;
      }
    } else if (arg == "--activity") {
      if (i + 1 >= argc) {
        error = "Missing value for --activity";
        return false;
      }
      options.activityPath = argv[++i];
    } else if (arg == "--attenuation") {
      if (i + 1 >= argc) {
        error = "Missing value for --attenuation";
        return false;
      }
      options.attenuationPath = argv[++i];
    } else if (arg == "--surface") {
      if (i + 1 >= argc) {
        error = "Missing value for --surface";
        return false;
      }
      options.surfacePath = argv[++i];
    } else if (arg == "--surface-label") {
      if (i + 1 >= argc) {
        error = "Missing value for --surface-label";
        return false;
      }
      options.surfaceLabel = argv[++i];
    } else if (arg == "--output") {
      if (i + 1 >= argc) {
        error = "Missing value for --output";
        return false;
      }
      options.outputDir = argv[++i];
    } else if (arg == "--dims") {
      if (!ParseTriplet(argc, argv, i, options.dims, error)) {
        return false;
      }
      options.dimsProvided = true;
    } else if (arg == "--spacing") {
      if (!ParseTriplet(argc, argv, i, options.spacing, "--spacing", error)) {
        return false;
      }
      options.spacingProvided = true;
    } else if (arg == "--origin") {
      if (!ParseTriplet(argc, argv, i, options.origin, "--origin", error)) {
        return false;
      }
      options.originProvided = true;
    } else if (arg == "--surface-scale") {
      if (!ParseTriplet(argc, argv, i, options.surfaceScale, "--surface-scale", error)) {
        return false;
      }
    } else if (arg == "--surface-translate") {
      if (!ParseTriplet(argc, argv, i, options.surfaceTranslate, "--surface-translate", error)) {
        return false;
      }
    } else if (arg == "--stable-surface-order") {
      options.stableSurfaceOrder = true;
    } else if (arg == "--no-stable-surface-order") {
      options.stableSurfaceOrder = false;
    } else if (arg == "--fit-volume-to-surfaces") {
      options.fitVolumeToSurfaces = true;
    } else if (arg == "--no-fit-volume-to-surfaces") {
      options.fitVolumeToSurfaces = false;
    } else if (arg == "--volume-background") {
      if (i + 1 >= argc) {
        error = "Missing value for --volume-background";
        return false;
      }
      try {
        options.volumeBackgroundValue = std::stod(argv[++i]);
      } catch (const std::exception &) {
        error = "Invalid number for --volume-background";
        return false;
      }
    } else if (arg == "--volume-background-epsilon") {
      if (i + 1 >= argc) {
        error = "Missing value for --volume-background-epsilon";
        return false;
      }
      try {
        options.volumeBackgroundEpsilon = std::stod(argv[++i]);
      } catch (const std::exception &) {
        error = "Invalid number for --volume-background-epsilon";
        return false;
      }
      if (options.volumeBackgroundEpsilon < 0.0) {
        error = "--volume-background-epsilon must be non-negative";
        return false;
      }
    } else if (arg == "--volume-fit-source") {
      if (i + 1 >= argc) {
        error = "Missing value for --volume-fit-source";
        return false;
      }
      options.volumeFitSource = argv[++i];
      if (options.volumeFitSource != "auto" &&
          options.volumeFitSource != "attenuation" &&
          options.volumeFitSource != "activity" &&
          options.volumeFitSource != "union") {
        error = "Invalid --volume-fit-source; expected auto, attenuation, activity, or union";
        return false;
      }
    } else if (arg == "--sample-vti-blocks" || arg == "--subsample-vti-blocks") {
      if (i + 1 >= argc) {
        error = "--sample-vti-blocks requires a positive integer count (e.g. --sample-vti-blocks 64)";
        return false;
      }
      try {
        options.sampleVtiBlockCount = std::stoi(argv[++i]);
      } catch (const std::exception &) {
        error = "Invalid integer for --sample-vti-blocks";
        return false;
      }
      if (options.sampleVtiBlockCount <= 0) {
        error = "--sample-vti-blocks count must be a positive integer";
        return false;
      }
    } else if (arg == "--anatomy-hierarchy") {
      options.anatomyHierarchy = true;
    } else if (arg == "--flat-blocks") {
      options.flatBlocks = true;
    } else if (arg == "--strict-anatomy") {
      options.strictAnatomy = true;
    } else if (arg == "--anatomy-config") {
      if (i + 1 >= argc) {
        error = "Missing value for --anatomy-config";
        return false;
      }
      options.anatomyConfigPath = argv[++i];
    } else if (arg == "--anatomy-report") {
      options.anatomyReport = true;
      if (i + 1 < argc) {
        const std::string next = argv[i + 1];
        if (!next.empty() && next[0] != '-') {
          options.anatomyReportPath = argv[++i];
        }
      }
    } else if (arg == "--anatomy-overrides" ||
               arg == "--anatomy-report-input") {
      if (i + 1 >= argc) {
        error = "Missing value for " + arg;
        return false;
      }
      options.anatomyOverridesPath = argv[++i];
    } else if (arg == "--anatomy-atlas") {
      options.anatomyAtlas = true;
      if (i + 1 < argc) {
        const std::string next = argv[i + 1];
        if (!next.empty() && next[0] != '-') {
          options.anatomyAtlasPath = argv[++i];
        }
      }
    } else {
      error = "Unknown argument: " + arg;
      return false;
    }
  }
  return true;
}

struct FrameInputs {
  std::string activityPath;
  std::string attenuationPath;
  std::vector<std::pair<std::string, std::string>> surfaceFiles;
};

bool HasExplicitInputs(const Options &options) {
  return !options.activityPath.empty() || !options.attenuationPath.empty() ||
         !options.surfacePath.empty();
}

bool ResolveAutoInputs(const Options &options,
                       std::filesystem::path &baseDir,
                       std::map<int, FrameInputs> &frames,
                       std::string &error) {
  if (options.id.empty()) {
    return true;
  }

  if (HasExplicitInputs(options)) {
    error = "Use either --id/--input or explicit file paths, not both.";
    return false;
  }

  if (options.inputDir.empty()) {
    error = "Missing --input for auto-discovery.";
    return false;
  }

  baseDir = std::filesystem::path(options.inputDir);
  std::filesystem::path candidate = baseDir / options.id;
  if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
    baseDir = candidate;
  }

  std::set<int> framesFound;

  const std::regex activityRegex("^" + options.id + "_act_([0-9]+)\\.bin$");
  const std::regex attenuationRegex("^" + options.id + "_atn_([0-9]+)\\.bin$");
  const std::regex surfaceRegex("^" + options.id + "_([0-9]+)_([^.]+)\\.raw$");

  std::error_code fsError;
  if (!std::filesystem::exists(baseDir, fsError)) {
    error = "Input directory does not exist: " + baseDir.string();
    return false;
  }

  for (const auto &entry : std::filesystem::directory_iterator(baseDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    std::smatch match;
    if (std::regex_match(name, match, activityRegex)) {
      const int frame = std::stoi(match[1].str());
      frames[frame].activityPath = entry.path().string();
      framesFound.insert(frame);
      continue;
    }
    if (std::regex_match(name, match, attenuationRegex)) {
      const int frame = std::stoi(match[1].str());
      frames[frame].attenuationPath = entry.path().string();
      framesFound.insert(frame);
      continue;
    }
    if (std::regex_match(name, match, surfaceRegex)) {
      const int frame = std::stoi(match[1].str());
      const std::string label = match[2].str();
      frames[frame].surfaceFiles.push_back({entry.path().string(), label});
      framesFound.insert(frame);
    }
  }

  if (framesFound.empty()) {
    error = "No matching input files found for id: " + options.id;
    return false;
  }

  return true;
}

bool AppendSurfaces(const std::string &path,
                    const std::string &groupLabel,
                    const std::array<double, 3> &scale,
                    const std::array<double, 3> &translate,
                    std::vector<SurfaceData> &surfaces,
                    std::string &error) {
  std::vector<SurfaceData> local;
  if (!RawSurfaceReader::ReadAll(path, groupLabel, scale, translate, local, error)) {
    return false;
  }
  const int baseIndex = static_cast<int>(surfaces.size());
  for (size_t i = 0; i < local.size(); ++i) {
    local[i].originalIndex = baseIndex + static_cast<int>(i);
  }
  surfaces.insert(surfaces.end(), local.begin(), local.end());
  return true;
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool LabelLess(const std::string &left, const std::string &right) {
  const std::string leftLower = LowerAscii(left);
  const std::string rightLower = LowerAscii(right);
  if (leftLower == rightLower) {
    return left < right;
  }
  return leftLower < rightLower;
}

std::string SurfaceBaseLabel(const std::string &label) {
  return label.empty() ? "surface" : label;
}

std::vector<std::string> NormalizeSurfaceLabels(const std::vector<SurfaceLabel> &labels) {
  std::map<std::string, int> labelCounts;
  std::vector<std::string> normalized;
  normalized.reserve(labels.size());
  for (const auto &surface : labels) {
    const std::string baseLabel = SurfaceBaseLabel(surface.label);
    const int count = ++labelCounts[baseLabel];
    normalized.push_back(count == 1 ? baseLabel : baseLabel + "_" + std::to_string(count));
  }
  return normalized;
}

void NormalizeSurfaceDataLabels(std::vector<SurfaceData> &surfaces) {
  std::map<std::string, int> labelCounts;
  for (auto &surface : surfaces) {
    const std::string baseLabel = SurfaceBaseLabel(surface.label);
    const int count = ++labelCounts[baseLabel];
    surface.label = count == 1 ? baseLabel : baseLabel + "_" + std::to_string(count);
  }
}

void IncludeBounds(SurfaceBounds &target, const SurfaceBounds &source) {
  if (!source.valid) {
    return;
  }
  if (!target.valid) {
    target = source;
    return;
  }
  for (int axis = 0; axis < 3; ++axis) {
    target.min[axis] = std::min(target.min[axis], source.min[axis]);
    target.max[axis] = std::max(target.max[axis], source.max[axis]);
  }
}

bool HasVolumeInputs(const std::map<int, FrameInputs> &frames,
                     const std::vector<int> &framesToProcess) {
  for (const int frame : framesToProcess) {
    const auto found = frames.find(frame);
    if (found == frames.end()) {
      continue;
    }
    if (!found->second.activityPath.empty() || !found->second.attenuationPath.empty()) {
      return true;
    }
  }
  return false;
}

bool HasSurfaceInputs(const std::map<int, FrameInputs> &frames,
                      const std::vector<int> &framesToProcess) {
  for (const int frame : framesToProcess) {
    const auto found = frames.find(frame);
    if (found == frames.end()) {
      continue;
    }
    if (!found->second.surfaceFiles.empty()) {
      return true;
    }
  }
  return false;
}

bool HasActivityInputs(const std::map<int, FrameInputs> &frames,
                       const std::vector<int> &framesToProcess) {
  for (const int frame : framesToProcess) {
    const auto found = frames.find(frame);
    if (found != frames.end() && !found->second.activityPath.empty()) {
      return true;
    }
  }
  return false;
}

bool HasAttenuationInputs(const std::map<int, FrameInputs> &frames,
                          const std::vector<int> &framesToProcess) {
  for (const int frame : framesToProcess) {
    const auto found = frames.find(frame);
    if (found != frames.end() && !found->second.attenuationPath.empty()) {
      return true;
    }
  }
  return false;
}

void SortSurfaceFiles(std::map<int, FrameInputs> &frames) {
  for (auto &entry : frames) {
    std::sort(entry.second.surfaceFiles.begin(),
              entry.second.surfaceFiles.end(),
              [](const auto &left, const auto &right) {
                if (LabelLess(left.second, right.second)) {
                  return true;
                }
                if (LabelLess(right.second, left.second)) {
                  return false;
                }
                return left.first < right.first;
              });
  }
}

bool ScanSurfacesForFrame(const FrameInputs &inputs,
                          const Options &options,
                          std::vector<std::string> &frameLabels,
                          SurfaceBounds &globalBounds,
                          std::string &error) {
  std::vector<SurfaceLabel> labels;
  for (const auto &surfaceFile : inputs.surfaceFiles) {
    SurfaceScanResult scan;
    if (!RawSurfaceReader::ScanAll(surfaceFile.first,
                                   surfaceFile.second,
                                   options.surfaceScale,
                                   options.surfaceTranslate,
                                   scan,
                                   error)) {
      return false;
    }
    labels.insert(labels.end(), scan.labels.begin(), scan.labels.end());
    IncludeBounds(globalBounds, scan.bounds);
  }
  frameLabels = NormalizeSurfaceLabels(labels);
  return true;
}

void IncludeVolumeBounds(VolumeIndexBounds &target, const VolumeIndexBounds &source) {
  if (!source.valid) {
    return;
  }
  if (!target.valid) {
    target = source;
    return;
  }
  for (int axis = 0; axis < 3; ++axis) {
    target.min[axis] = std::min(target.min[axis], source.min[axis]);
    target.max[axis] = std::max(target.max[axis], source.max[axis]);
  }
}

bool CollectVolumeFitPaths(const std::map<int, FrameInputs> &frames,
                           const std::vector<int> &framesToProcess,
                           const Options &options,
                           std::vector<std::string> &paths,
                           std::string &error) {
  const bool hasActivity = HasActivityInputs(frames, framesToProcess);
  const bool hasAttenuation = HasAttenuationInputs(frames, framesToProcess);
  std::string source = options.volumeFitSource;
  if (source == "auto") {
    source = hasAttenuation ? "attenuation" : "activity";
  }

  if (source == "attenuation" && !hasAttenuation) {
    error = "No attenuation files available for --volume-fit-source attenuation.";
    return false;
  }
  if (source == "activity" && !hasActivity) {
    error = "No activity files available for --volume-fit-source activity.";
    return false;
  }
  if (source == "union" && !hasActivity && !hasAttenuation) {
    error = "No volume files available for --volume-fit-source union.";
    return false;
  }

  for (const int frame : framesToProcess) {
    const auto found = frames.find(frame);
    if (found == frames.end()) {
      continue;
    }
    const FrameInputs &inputs = found->second;
    if ((source == "activity" || source == "union") && !inputs.activityPath.empty()) {
      paths.push_back(inputs.activityPath);
    }
    if ((source == "attenuation" || source == "union") && !inputs.attenuationPath.empty()) {
      paths.push_back(inputs.attenuationPath);
    }
  }

  return true;
}

bool ScanVolumeBoundsForFit(const std::map<int, FrameInputs> &frames,
                            const std::vector<int> &framesToProcess,
                            const Options &options,
                            VolumeIndexBounds &globalBounds,
                            std::string &error) {
  std::vector<std::string> paths;
  if (!CollectVolumeFitPaths(frames, framesToProcess, options, paths, error)) {
    return false;
  }

  for (const auto &path : paths) {
    VolumeIndexBounds fileBounds;
    if (!BinVolumeReader::ScanNonBackgroundBounds(path,
                                                  options.dims,
                                                  options.volumeBackgroundValue,
                                                  options.volumeBackgroundEpsilon,
                                                  fileBounds,
                                                  error)) {
      return false;
    }
    IncludeVolumeBounds(globalBounds, fileBounds);
  }

  return true;
}

SurfaceData MakeEmptySurface(const std::string &label) {
  SurfaceData surface;
  surface.label = label;
  surface.originalLabel = label;
  surface.originalIndex = -1;
  surface.placeholder = true;
  surface.data = vtkSmartPointer<vtkPolyData>::New();
  return surface;
}

std::vector<SurfaceData> OrderSurfacesForFrame(std::vector<SurfaceData> surfaces,
                                               const std::vector<std::string> &globalOrder) {
  NormalizeSurfaceDataLabels(surfaces);
  if (globalOrder.empty()) {
    std::sort(surfaces.begin(), surfaces.end(), [](const auto &left, const auto &right) {
      return LabelLess(left.label, right.label);
    });
    return surfaces;
  }

  std::map<std::string, SurfaceData> surfacesByLabel;
  for (const auto &surface : surfaces) {
    surfacesByLabel[surface.label] = surface;
  }

  std::vector<SurfaceData> ordered;
  ordered.reserve(globalOrder.size() + surfacesByLabel.size());
  for (const auto &label : globalOrder) {
    const auto found = surfacesByLabel.find(label);
    if (found == surfacesByLabel.end()) {
      ordered.push_back(MakeEmptySurface(label));
      continue;
    }
    ordered.push_back(found->second);
    surfacesByLabel.erase(found);
  }

  for (const auto &extra : surfacesByLabel) {
    ordered.push_back(extra.second);
  }
  return ordered;
}

bool ValidateVolumeDimensions(const Options &options, std::string &error) {
  if (!options.dimsProvided) {
    error = "Missing --dims for volume input.";
    return false;
  }
  for (const int dim : options.dims) {
    if (dim <= 0) {
      error = "Volume dimensions must be positive.";
      return false;
    }
  }
  return true;
}

bool FitVolumeToSurfaceBounds(const Options &options,
                              const SurfaceBounds &bounds,
                              const VolumeIndexBounds &volumeBounds,
                              std::array<double, 3> &spacing,
                              std::array<double, 3> &origin,
                              std::string &error) {
  if (!options.fitVolumeToSurfaces || !bounds.valid) {
    return true;
  }

  if (!volumeBounds.valid) {
    if (!options.originProvided) {
      origin = bounds.min;
    }
    if (!options.spacingProvided) {
      for (int axis = 0; axis < 3; ++axis) {
        const double extent = bounds.max[axis] - bounds.min[axis];
        if (extent <= 0.0) {
          error = "Cannot fit volume to degenerate surface bounds.";
          return false;
        }
        spacing[axis] = extent / static_cast<double>(options.dims[axis]);
      }
    }
    return true;
  }

  std::array<double, 3> fittedSpacing = spacing;
  if (!options.spacingProvided) {
    for (int axis = 0; axis < 3; ++axis) {
      const double extent = bounds.max[axis] - bounds.min[axis];
      const int occupiedCells = volumeBounds.max[axis] - volumeBounds.min[axis] + 1;
      if (extent <= 0.0) {
        error = "Cannot fit volume to degenerate surface bounds.";
        return false;
      }
      if (occupiedCells <= 0) {
        error = "Cannot fit volume to invalid occupied voxel bounds.";
        return false;
      }
      fittedSpacing[axis] = extent / static_cast<double>(occupiedCells);
    }
  }

  spacing = fittedSpacing;
  if (!options.originProvided) {
    for (int axis = 0; axis < 3; ++axis) {
      origin[axis] = bounds.min[axis] -
                     static_cast<double>(volumeBounds.min[axis]) * spacing[axis];
    }
  }

  return true;
}

std::string PadFrame(int frame) {
  std::string value = std::to_string(frame);
  if (value.size() >= 3) {
    return value;
  }
  return std::string(3 - value.size(), '0') + value;
}

std::string PadNumber(int value, size_t width) {
  std::ostringstream out;
  out << std::setw(static_cast<int>(width)) << std::setfill('0') << value;
  return out.str();
}

std::string FormatDouble(double value) {
  std::ostringstream out;
  out << std::setprecision(12) << value;
  return out.str();
}

std::string FormatIntTriplet(const std::array<int, 3> &values) {
  return std::to_string(values[0]) + "," + std::to_string(values[1]) + "," +
         std::to_string(values[2]);
}

std::string FormatDoubleTriplet(const std::array<double, 3> &values) {
  return FormatDouble(values[0]) + "," + FormatDouble(values[1]) + "," +
         FormatDouble(values[2]);
}

VolumeIndexBounds FullVolumeBounds(const std::array<int, 3> &dims) {
  VolumeIndexBounds bounds;
  bounds.valid = true;
  bounds.min = {0, 0, 0};
  bounds.max = {dims[0] - 1, dims[1] - 1, dims[2] - 1};
  return bounds;
}

std::array<int, 3> BoundsExtent(const VolumeIndexBounds &bounds) {
  return {bounds.max[0] - bounds.min[0] + 1,
          bounds.max[1] - bounds.min[1] + 1,
          bounds.max[2] - bounds.min[2] + 1};
}

// ---------------------------------------------------------------------------
// Uniform grid subdivision helpers
// ---------------------------------------------------------------------------

// Find (nx, ny, nz) with nx*ny*nz == targetN that produces the most
// cube-like blocks given the axis extents.  Assigns the largest factor to
// the longest axis so block aspect ratios are minimised.
std::array<int, 3> ComputeGridDivisions(int targetN,
                                        const std::array<int, 3> &extents) {
  std::array<int, 3> bestDiv = {1, 1, targetN};
  double bestAspect = std::numeric_limits<double>::max();

  for (int a = 1; a <= targetN; ++a) {
    if (targetN % a != 0) {
      continue;
    }
    const int remA = targetN / a;
    for (int b = a; b <= remA; ++b) {
      if (remA % b != 0) {
        continue;
      }
      const int c = remA / b;
      if (c < b) {
        break;
      }
      // a <= b <= c, a*b*c == targetN
      // Assign largest factor to longest axis
      std::array<int, 3> sortedIdx = {0, 1, 2};
      std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int x, int y) {
        return extents[x] > extents[y];
      });
      std::array<int, 3> divs;
      divs[sortedIdx[0]] = c;
      divs[sortedIdx[1]] = b;
      divs[sortedIdx[2]] = a;

      const double bx =
          static_cast<double>(std::max(extents[0], 1)) / static_cast<double>(divs[0]);
      const double by =
          static_cast<double>(std::max(extents[1], 1)) / static_cast<double>(divs[1]);
      const double bz =
          static_cast<double>(std::max(extents[2], 1)) / static_cast<double>(divs[2]);
      const double maxB = std::max({bx, by, bz});
      const double minB = std::min({bx, by, bz});
      const double aspect =
          (minB > 0.0) ? maxB / minB : std::numeric_limits<double>::max();

      if (aspect < bestAspect) {
        bestAspect = aspect;
        bestDiv = divs;
      }
    }
  }

  return bestDiv;
}

std::string GridBlockFileName(const std::string &scalarName,
                              const std::array<int, 3> &gridIjk) {
  return scalarName + "_grid_" + PadNumber(gridIjk[0], 3) + "_" +
         PadNumber(gridIjk[1], 3) + "_" + PadNumber(gridIjk[2], 3) + ".vti";
}

void AddMetadata(vtkStringArray *metadata,
                 const std::string &key,
                 const std::string &value) {
  metadata->InsertNextValue(key + "=" + value);
}

void AddVtiGridBlockMetadata(vtkSmartPointer<vtkImageData> image,
                             const std::string &sourcePath,
                             const std::string &scalarName,
                             const std::array<int, 3> &sourceDims,
                             int frame,
                             const std::array<int, 3> &gridIjk,
                             int gridLinearIndex,
                             const std::array<int, 3> &gridDivisions,
                             const std::array<int, 3> &blockStart,
                             const std::array<int, 3> &blockDims,
                             int requestedCount,
                             const VolumeIndexBounds &sampleBounds,
                             const std::string &sampleBoundsKind,
                             const std::array<double, 3> &spacing,
                             const std::array<double, 3> &origin,
                             const VolumeBlockStats &stats) {
  vtkSmartPointer<vtkStringArray> metadata = vtkSmartPointer<vtkStringArray>::New();
  metadata->SetName("xcat2vtk_sample_metadata");

  const std::array<int, 3> blockEnd = {
      blockStart[0] + blockDims[0] - 1,
      blockStart[1] + blockDims[1] - 1,
      blockStart[2] + blockDims[2] - 1};
  const std::array<double, 3> blockPhysicalOrigin = {
      origin[0] + static_cast<double>(blockStart[0]) * spacing[0],
      origin[1] + static_cast<double>(blockStart[1]) * spacing[1],
      origin[2] + static_cast<double>(blockStart[2]) * spacing[2]};
  const std::array<double, 3> blockPhysicalMax = {
      origin[0] + static_cast<double>(blockEnd[0] + 1) * spacing[0],
      origin[1] + static_cast<double>(blockEnd[1] + 1) * spacing[1],
      origin[2] + static_cast<double>(blockEnd[2] + 1) * spacing[2]};

  const int totalCount =
      gridDivisions[0] * gridDivisions[1] * gridDivisions[2];

  AddMetadata(metadata, "grid_label",
              scalarName + "_grid_" + PadNumber(gridIjk[0], 3) + "_" +
                  PadNumber(gridIjk[1], 3) + "_" + PadNumber(gridIjk[2], 3));
  AddMetadata(metadata, "grid_strategy", "uniform bounding-box subdivision");
  AddMetadata(metadata, "grid_cell_ijk", FormatIntTriplet(gridIjk));
  AddMetadata(metadata, "grid_linear_index", std::to_string(gridLinearIndex));
  AddMetadata(metadata, "grid_divisions_ijk", FormatIntTriplet(gridDivisions));
  AddMetadata(metadata, "grid_total_count", std::to_string(totalCount));
  AddMetadata(metadata, "grid_requested_count", std::to_string(requestedCount));
  AddMetadata(metadata, "source_path", sourcePath);
  AddMetadata(metadata, "source_scalar", scalarName);
  AddMetadata(metadata, "frame", std::to_string(frame));
  AddMetadata(metadata, "source_dims_ijk", FormatIntTriplet(sourceDims));
  AddMetadata(metadata, "block_start_ijk", FormatIntTriplet(blockStart));
  AddMetadata(metadata, "block_end_ijk", FormatIntTriplet(blockEnd));
  AddMetadata(metadata, "block_dims_ijk", FormatIntTriplet(blockDims));
  AddMetadata(metadata, "sample_bounds_kind", sampleBoundsKind);
  AddMetadata(metadata, "sample_bounds_min_ijk", FormatIntTriplet(sampleBounds.min));
  AddMetadata(metadata, "sample_bounds_max_ijk", FormatIntTriplet(sampleBounds.max));
  AddMetadata(metadata, "spacing", FormatDoubleTriplet(spacing));
  AddMetadata(metadata, "block_physical_origin", FormatDoubleTriplet(blockPhysicalOrigin));
  AddMetadata(metadata, "block_physical_max", FormatDoubleTriplet(blockPhysicalMax));

  const double nonBgFraction =
      (stats.voxelCount > 0)
          ? static_cast<double>(stats.nonBackgroundCount) /
                static_cast<double>(stats.voxelCount)
          : 0.0;
  AddMetadata(metadata, "non_background_fraction", FormatDouble(nonBgFraction));
  AddMetadata(metadata, "non_background_voxels",
              std::to_string(stats.nonBackgroundCount));
  AddMetadata(metadata, "voxel_count", std::to_string(stats.voxelCount));
  AddMetadata(metadata, "scalar_min", FormatDouble(stats.minValue));
  AddMetadata(metadata, "scalar_max", FormatDouble(stats.maxValue));
  AddMetadata(metadata, "scalar_mean", FormatDouble(stats.meanValue));
  AddMetadata(metadata, "distinct_scalar_values_observed",
              std::to_string(stats.distinctValueCount));
  AddMetadata(metadata, "distinct_scalar_values_capped",
              stats.distinctValueLimitReached ? "true" : "false");

  image->GetFieldData()->AddArray(metadata);
}

bool WriteVtiGridBlocks(const std::string &volumePath,
                        const std::string &scalarName,
                        int frame,
                        const std::filesystem::path &frameDir,
                        const Options &options,
                        const std::array<double, 3> &spacing,
                        const std::array<double, 3> &origin,
                        std::string &error) {
  VolumeIndexBounds occupiedBounds;
  if (!BinVolumeReader::ScanNonBackgroundBounds(volumePath,
                                                options.dims,
                                                options.volumeBackgroundValue,
                                                options.volumeBackgroundEpsilon,
                                                occupiedBounds,
                                                error)) {
    return false;
  }

  VolumeIndexBounds sampleBounds;
  std::string sampleBoundsKind;
  if (occupiedBounds.valid) {
    sampleBounds = occupiedBounds;
    sampleBoundsKind = "non_background";
  } else {
    sampleBounds = FullVolumeBounds(options.dims);
    sampleBoundsKind = "full_volume";
    std::cerr << "Warning: no non-background voxels found in " << volumePath
              << "; grid subdivision uses the full volume bounds.\n";
  }

  const std::array<int, 3> extent = {
      sampleBounds.max[0] - sampleBounds.min[0] + 1,
      sampleBounds.max[1] - sampleBounds.min[1] + 1,
      sampleBounds.max[2] - sampleBounds.min[2] + 1};

  const std::array<int, 3> divisions =
      ComputeGridDivisions(options.sampleVtiBlockCount, extent);
  const int totalBlocks = divisions[0] * divisions[1] * divisions[2];

  if (totalBlocks != options.sampleVtiBlockCount) {
    std::cout << "Note: " << options.sampleVtiBlockCount
              << " factors into a " << divisions[0] << "x" << divisions[1]
              << "x" << divisions[2] << " grid (" << totalBlocks
              << " blocks) for " << scalarName << ".\n";
  }

  // Base block size (last block per axis may be smaller)
  const std::array<int, 3> blockDims = {
      (extent[0] + divisions[0] - 1) / divisions[0],
      (extent[1] + divisions[1] - 1) / divisions[1],
      (extent[2] + divisions[2] - 1) / divisions[2]};

  int linearIndex = 0;
  for (int iz = 0; iz < divisions[2]; ++iz) {
    for (int iy = 0; iy < divisions[1]; ++iy) {
      for (int ix = 0; ix < divisions[0]; ++ix) {
        const std::array<int, 3> gridIjk = {ix, iy, iz};
        const std::array<int, 3> blockStart = {
            sampleBounds.min[0] + ix * blockDims[0],
            sampleBounds.min[1] + iy * blockDims[1],
            sampleBounds.min[2] + iz * blockDims[2]};
        // Clamp last block so it does not exceed the bounding box
        const std::array<int, 3> actualDims = {
            std::min(blockDims[0], sampleBounds.max[0] - blockStart[0] + 1),
            std::min(blockDims[1], sampleBounds.max[1] - blockStart[1] + 1),
            std::min(blockDims[2], sampleBounds.max[2] - blockStart[2] + 1)};

        VolumeBlockStats stats;
        vtkSmartPointer<vtkImageData> image = BinVolumeReader::ReadBlock(
            volumePath,
            options.dims,
            spacing,
            origin,
            blockStart,
            actualDims,
            scalarName,
            options.volumeBackgroundValue,
            options.volumeBackgroundEpsilon,
            &stats,
            error);
        if (!image) {
          return false;
        }

        AddVtiGridBlockMetadata(image,
                                volumePath,
                                scalarName,
                                options.dims,
                                frame,
                                gridIjk,
                                linearIndex,
                                divisions,
                                blockStart,
                                actualDims,
                                options.sampleVtiBlockCount,
                                sampleBounds,
                                sampleBoundsKind,
                                spacing,
                                origin,
                                stats);

        const std::filesystem::path blockPath =
            frameDir / "vti_samples" / scalarName /
            GridBlockFileName(scalarName, gridIjk);
        if (!VtkSceneWriter::WriteImageData(blockPath.string(), image, error)) {
          return false;
        }

        ++linearIndex;
      }
    }
  }

  std::cout << "Wrote " << totalBlocks << " VTI grid blocks ("
            << divisions[0] << "x" << divisions[1] << "x" << divisions[2]
            << ") for " << scalarName << " -> "
            << (frameDir / "vti_samples" / scalarName).string() << "\n";
  return true;
}

bool WritePvd(const std::filesystem::path &baseOutput,
              const std::string &id,
              const std::vector<int> &frames,
              std::string &error) {
  if (frames.empty()) {
    return true;
  }
  std::filesystem::path pvdPath = baseOutput / (id.empty() ? "scene.pvd" : id + "_scene.pvd");
  std::ofstream out(pvdPath);
  if (!out.is_open()) {
    error = "Failed to write PVD: " + pvdPath.string();
    return false;
  }
  out << "<?xml version=\"1.0\"?>\n";
  out << "<VTKFile type=\"Collection\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
  out << "  <Collection>\n";
  for (const int frame : frames) {
    const std::string frameLabel = "time_" + PadFrame(frame);
    out << "    <DataSet timestep=\"" << frame << "\" group=\"\" part=\"0\" file=\""
        << frameLabel << "/scene.vtm\"/>\n";
  }
  out << "  </Collection>\n";
  out << "</VTKFile>\n";
  return true;
}
} // namespace

int main(int argc, char **argv) {
  Options options;
  std::string error;

  if (!ParseArgs(argc, argv, options, error)) {
    std::cerr << "Error: " << error << "\n";
    std::cerr << "Use --help for usage.\n";
    return 1;
  }

  if (options.showHelp) {
    PrintHelp();
    return 0;
  }

  if (options.anatomyHierarchy && options.flatBlocks) {
    std::cerr << "Error: Use either --anatomy-hierarchy or --flat-blocks, not both.\n";
    return 1;
  }

  if ((options.strictAnatomy || !options.anatomyConfigPath.empty() ||
       options.anatomyReport ||
       !options.anatomyOverridesPath.empty() ||
       options.anatomyAtlas) &&
      !options.anatomyHierarchy) {
    std::cerr << "Error: --strict-anatomy, --anatomy-config, --anatomy-report, "
                 "--anatomy-overrides, and --anatomy-atlas require "
                 "--anatomy-hierarchy.\n";
    return 1;
  }

  if (!options.anatomyOverridesPath.empty() &&
      options.anatomyAtlas) {
    std::cerr << "Error: Use --anatomy-atlas as the reusable override source; "
                 "do not combine it with --anatomy-overrides.\n";
    return 1;
  }

  std::filesystem::path autoBaseDir;
  std::map<int, FrameInputs> autoFrames;
  if (!ResolveAutoInputs(options, autoBaseDir, autoFrames, error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  if (options.outputDir.empty()) {
    std::cerr << "Error: Missing required --output directory.\n";
    return 1;
  }

  std::vector<int> framesToProcess;
  std::map<int, FrameInputs> frames;

  if (!options.id.empty()) {
    frames = autoFrames;
    for (const auto &entry : frames) {
      framesToProcess.push_back(entry.first);
    }
    std::sort(framesToProcess.begin(), framesToProcess.end());
    if (options.frameProvided) {
      if (frames.find(options.frame) == frames.end()) {
        std::cerr << "Error: Requested --frame not found in input directory.\n";
        return 1;
      }
      framesToProcess = {options.frame};
    }
  } else {
    FrameInputs single;
    single.activityPath = options.activityPath;
    single.attenuationPath = options.attenuationPath;
    if (!options.surfacePath.empty()) {
      single.surfaceFiles.push_back({options.surfacePath, options.surfaceLabel});
    }
    frames[0] = single;
    framesToProcess = {0};
  }

  SortSurfaceFiles(frames);

  const bool hasVolume = HasVolumeInputs(frames, framesToProcess);
  if (hasVolume && !ValidateVolumeDimensions(options, error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::vector<std::string> globalSurfaceOrder;
  SurfaceBounds globalSurfaceBounds;
  const bool needsSurfaceBoundsForVolume =
      hasVolume && options.fitVolumeToSurfaces &&
      (!options.spacingProvided || !options.originProvided);
  if (HasSurfaceInputs(frames, framesToProcess) &&
      (options.stableSurfaceOrder || needsSurfaceBoundsForVolume)) {
    std::vector<std::string> allLabels;
    for (const int frame : framesToProcess) {
      std::vector<std::string> frameLabels;
      if (!ScanSurfacesForFrame(frames[frame],
                                options,
                                frameLabels,
                                globalSurfaceBounds,
                                error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
      allLabels.insert(allLabels.end(), frameLabels.begin(), frameLabels.end());
    }
    std::sort(allLabels.begin(), allLabels.end(), LabelLess);
    allLabels.erase(std::unique(allLabels.begin(), allLabels.end()), allLabels.end());
    globalSurfaceOrder = allLabels;
  }

  std::array<double, 3> volumeSpacing = options.spacing;
  std::array<double, 3> volumeOrigin = options.origin;
  VolumeIndexBounds globalVolumeBounds;
  if (needsSurfaceBoundsForVolume && globalSurfaceBounds.valid) {
    if (!ScanVolumeBoundsForFit(frames,
                                framesToProcess,
                                options,
                                globalVolumeBounds,
                                error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
    if (!globalVolumeBounds.valid) {
      std::cerr << "Warning: no non-background voxels found; fitting the full volume grid "
                   "to surface bounds.\n";
    }
  }
  if (hasVolume && !FitVolumeToSurfaceBounds(options,
                                             globalSurfaceBounds,
                                             globalVolumeBounds,
                                             volumeSpacing,
                                             volumeOrigin,
                                             error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::filesystem::path baseOutput(options.outputDir);
  if (!options.id.empty()) {
    baseOutput /= options.id;
  }

  AnatomyConfig anatomyConfig;
  AnatomyReportOverrides anatomyOverrides;
  AnatomySummary anatomySummary;
  size_t anatomyOverrideApplyCount = 0;
  size_t anatomyAtlasAppendCount = 0;
  std::ofstream anatomyReport;
  const std::string anatomyReportPath =
      options.anatomyReport ? ResolveAnatomyReportPath(options) : std::string();
  std::string anatomyOverrideInputPath = options.anatomyOverridesPath;
  const std::string anatomyAtlasPath =
      options.anatomyAtlas ? ResolveAnatomyAtlasPath(options) : std::string();
  if (anatomyOverrideInputPath.empty() && !anatomyAtlasPath.empty() &&
      std::filesystem::exists(anatomyAtlasPath)) {
    anatomyOverrideInputPath = anatomyAtlasPath;
  }
  if (options.anatomyHierarchy) {
    if (!LoadDefaultOrConfiguredAnatomyConfig(options.anatomyConfigPath,
                                              anatomyConfig,
                                              error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
    if (!anatomyOverrideInputPath.empty()) {
      if (!LoadAnatomyReportOverrides(anatomyOverrideInputPath,
                                      anatomyOverrides,
                                      error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
    }
    if (!anatomyReportPath.empty()) {
      const std::filesystem::path reportPath(anatomyReportPath);
      const std::filesystem::path reportParent = reportPath.parent_path();
      if (!reportParent.empty()) {
        std::error_code fsError;
        std::filesystem::create_directories(reportParent, fsError);
        if (fsError) {
          std::cerr << "Error: Failed to create anatomy report directory: "
                    << reportParent.string() << "\n";
          return 1;
        }
      }
      anatomyReport.open(reportPath, std::ios::trunc);
      if (!anatomyReport.is_open()) {
        std::cerr << "Error: Failed to write anatomy report: "
                  << reportPath.string() << "\n";
        return 1;
      }
      WriteAnatomyReportHeader(anatomyReport);
    }
  }

  for (const int frame : framesToProcess) {
    const auto &inputs = frames[frame];
    vtkSmartPointer<vtkImageData> activity;
    vtkSmartPointer<vtkImageData> attenuation;
    std::vector<SurfaceData> surfaces;

    if (!inputs.activityPath.empty()) {
      activity = BinVolumeReader::Read(
          inputs.activityPath,
          options.dims,
          volumeSpacing,
          volumeOrigin,
          "activity",
          error);
      if (!activity) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
    }

    if (!inputs.attenuationPath.empty()) {
      attenuation = BinVolumeReader::Read(
          inputs.attenuationPath,
          options.dims,
          volumeSpacing,
          volumeOrigin,
          "attenuation",
          error);
      if (!attenuation) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
    }

    for (const auto &surfaceFile : inputs.surfaceFiles) {
      if (!AppendSurfaces(surfaceFile.first,
                          surfaceFile.second,
                          options.surfaceScale,
                          options.surfaceTranslate,
                          surfaces,
                          error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
    }

    if (options.stableSurfaceOrder) {
      surfaces = OrderSurfacesForFrame(surfaces, globalSurfaceOrder);
    }

    const std::filesystem::path frameDir = baseOutput / ("time_" + PadFrame(frame));
    VtkSceneWriteOptions sceneWriteOptions;
    sceneWriteOptions.anatomyHierarchy = options.anatomyHierarchy && !options.flatBlocks;
    sceneWriteOptions.strictAnatomy = options.strictAnatomy;
    sceneWriteOptions.anatomyConfig =
        sceneWriteOptions.anatomyHierarchy ? &anatomyConfig : nullptr;
    sceneWriteOptions.anatomyOverrides =
        sceneWriteOptions.anatomyHierarchy && !anatomyOverrideInputPath.empty()
            ? &anatomyOverrides
            : nullptr;
    sceneWriteOptions.anatomyReport =
        sceneWriteOptions.anatomyHierarchy && anatomyReport.is_open()
            ? &anatomyReport
            : nullptr;
    sceneWriteOptions.anatomySummary =
        sceneWriteOptions.anatomyHierarchy ? &anatomySummary : nullptr;
    sceneWriteOptions.anatomyOverrideApplyCount =
        sceneWriteOptions.anatomyHierarchy ? &anatomyOverrideApplyCount : nullptr;

    if (!VtkSceneWriter::WriteScene(
            frameDir.string(),
            "scene.vtm",
            activity,
            attenuation,
            surfaces,
            error,
            sceneWriteOptions)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }

    if (options.sampleVtiBlockCount > 0) {
      if (!inputs.activityPath.empty() &&
          !WriteVtiGridBlocks(inputs.activityPath,
                              "activity",
                              frame,
                              frameDir,
                              options,
                              volumeSpacing,
                              volumeOrigin,
                              error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
      if (!inputs.attenuationPath.empty() &&
          !WriteVtiGridBlocks(inputs.attenuationPath,
                              "attenuation",
                              frame,
                              frameDir,
                              options,
                              volumeSpacing,
                              volumeOrigin,
                              error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
      }
    }
  }

  if (!WritePvd(baseOutput, options.id, framesToProcess, error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  if (options.anatomyHierarchy && !anatomyAtlasPath.empty()) {
    if (!UpdateAnatomyAtlasFile(anatomyAtlasPath,
                                anatomySummary.unclassifiedClassifications,
                                options.id,
                                anatomyAtlasAppendCount,
                                error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
  }

  std::cout << "Wrote outputs under: " << baseOutput.string() << "\n";
  if (options.anatomyHierarchy) {
    std::cout << "\nAnatomical hierarchy summary\n";
    std::cout << "-----------------------------\n";
    std::cout << "Input blocks: " << anatomySummary.inputBlocks << "\n";
    std::cout << "Classified blocks: " << anatomySummary.classifiedBlocks << "\n";
    std::cout << "Unclassified blocks: " << anatomySummary.unclassifiedBlocks << "\n";
    std::cout << "Unique normalized families: "
              << anatomySummary.uniqueNormalizedFamilies.size() << "\n";
    if (!anatomyOverrideInputPath.empty()) {
      std::cout << "Anatomy override rows loaded: "
                << anatomyOverrides.rowCount << "\n";
      if (anatomyOverrides.skippedRows > 0) {
        std::cout << "Anatomy override rows skipped: "
                  << anatomyOverrides.skippedRows << "\n";
      }
      std::cout << "Anatomy overrides applied: "
                << anatomyOverrideApplyCount << "\n";
    }
    if (!anatomyReportPath.empty()) {
      std::cout << "Anatomy report path: "
                << anatomyReportPath << "\n";
    }
    if (!anatomyAtlasPath.empty()) {
      std::cout << "Anatomy atlas path: "
                << anatomyAtlasPath << "\n";
      std::cout << "Anatomy atlas new pending rows: "
                << anatomyAtlasAppendCount << "\n";
    }
    for (const auto &entry : anatomySummary.systemCounts) {
      std::cout << entry.first << " blocks: " << entry.second << "\n";
    }
  }
  return 0;
}
