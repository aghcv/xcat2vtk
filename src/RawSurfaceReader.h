#pragma once

#include <string>
#include <vector>

#include "SurfaceTypes.h"

class RawSurfaceReader {
public:
  static bool ReadAll(
      const std::string &path,
  const std::string &groupLabel,
      std::vector<SurfaceData> &surfaces,
      std::string &error);
};
