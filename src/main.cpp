#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "BinVolumeReader.h"
#include "RawSurfaceReader.h"
#include "VtkSceneWriter.h"

#include <vtkImageData.h>
#include <vtkPolyData.h>

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
  bool showHelp = false;
};

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
    if (!VtkSceneWriter::WriteScene(
            frameDir.string(),
            "scene.vtm",
            activity,
            attenuation,
            surfaces,
            error)) {
      std::cerr << "Error: " << error << "\n";
      return 1;
    }
  }

  if (!WritePvd(baseOutput, options.id, framesToProcess, error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << "Wrote outputs under: " << baseOutput.string() << "\n";
  return 0;
}
