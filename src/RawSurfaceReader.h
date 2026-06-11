#pragma once

#include <array>
#include <string>
#include <vector>

#include "SurfaceTypes.h"

class RawSurfaceReader {
public:
  static bool ReadAll(
      const std::string &path,
      const std::string &groupLabel,
      const std::array<double, 3> &scale,
      const std::array<double, 3> &translate,
      std::vector<SurfaceData> &surfaces,
      std::string &error);
};
