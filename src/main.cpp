#include <algorithm>
#include <array>
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
  bool dimsProvided = false;
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
    } else if (arg == "--origin") {
      if (!ParseTriplet(argc, argv, i, options.origin, "--origin", error)) {
        return false;
      }
    } else if (arg == "--surface-scale") {
      if (!ParseTriplet(argc, argv, i, options.surfaceScale, "--surface-scale", error)) {
        return false;
      }
    } else if (arg == "--surface-translate") {
      if (!ParseTriplet(argc, argv, i, options.surfaceTranslate, "--surface-translate", error)) {
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

  const bool hasVolume = !options.activityPath.empty() || !options.attenuationPath.empty();
  if (hasVolume && !options.dimsProvided) {
    std::cerr << "Error: Missing --dims for volume input.\n";
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
          options.spacing,
          options.origin,
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
          options.spacing,
          options.origin,
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
