#pragma once

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct AnatomyAlias {
  std::string normalizedName;
  std::string canonicalName;
  std::string system;
  std::string subsystem;
  std::string anatomicalRegion;
  std::string structureType;
  std::string laterality;
  std::string temporalPhase;
  std::string hierarchyPath;
  std::string secondarySystems;
  std::string confidence = "exact";
};

struct AnatomyRule {
  std::string id;
  int priority = 0;
  std::string pattern;
  std::string normalizedName;
  std::string canonicalName;
  std::string system;
  std::string subsystem;
  std::string anatomicalRegion;
  std::string structureType;
  std::string laterality;
  std::string temporalPhase;
  std::string hierarchyPath;
  std::string secondarySystems;
  std::string confidence;
};

struct AnatomyConfig {
  std::map<std::string, AnatomyAlias> aliases;
  std::vector<AnatomyRule> rules;
  std::map<std::string, std::string> systemRoots;
};

struct AnatomyClassification {
  std::string originalName;
  std::string normalizedName;
  std::string canonicalName;
  std::string system = "unclassified";
  std::string subsystem = "unclassified";
  std::string anatomicalRegion = "unspecified";
  std::string structureType = "other";
  std::string laterality = "unspecified";
  std::string pieceNumber;
  std::string temporalPhase = "static";
  std::string confidence = "unclassified";
  std::string classificationSource = "unclassified";
  std::string hierarchyPath = "99_Unclassified";
  std::string displayName;
  std::string canonicalIdentifier;
  std::string secondarySystems;
  int originalBlockIndex = -1;
  std::string sourceFile;
  bool unclassified = true;
};

struct AnatomySummary {
  size_t inputBlocks = 0;
  size_t classifiedBlocks = 0;
  size_t unclassifiedBlocks = 0;
  std::set<std::string> uniqueNormalizedFamilies;
  std::map<std::string, size_t> systemCounts;
  std::vector<std::string> unclassifiedLabels;
  std::vector<AnatomyClassification> unclassifiedClassifications;

  void Add(const AnatomyClassification &classification);
};

struct AnatomyReportOverrideColumns {
  bool normalizedName = false;
  bool canonicalName = false;
  bool system = false;
  bool subsystem = false;
  bool anatomicalRegion = false;
  bool structureType = false;
  bool laterality = false;
  bool pieceNumber = false;
  bool temporalPhase = false;
  bool hierarchyPath = false;
  bool confidence = false;
  bool classificationSource = false;
  bool sourceFile = false;
};

struct AnatomyReportOverrides {
  AnatomyReportOverrideColumns columns;
  size_t rowCount = 0;
  size_t skippedRows = 0;
  std::map<std::tuple<std::string, int, std::string>, AnatomyClassification>
      bySourceIndexAndName;
  std::map<std::pair<int, std::string>, AnatomyClassification> byIndexAndName;
  std::map<std::string, AnatomyClassification> byOriginalName;
};

class AnatomyClassifier {
public:
  explicit AnatomyClassifier(AnatomyConfig config);

  AnatomyClassification Classify(
      const std::string &name,
      int originalBlockIndex = -1,
      const std::string &sourceFile = std::string()) const;

  const AnatomyConfig &config() const { return config_; }

private:
  AnatomyConfig config_;
};

AnatomyConfig DefaultAnatomyConfig();

bool LoadAnatomyConfigFile(
    const std::string &path,
    AnatomyConfig &config,
    std::string &error);

bool LoadDefaultOrConfiguredAnatomyConfig(
    const std::string &path,
    AnatomyConfig &config,
    std::string &error);

bool LoadAnatomyReportOverrides(
    const std::string &path,
    AnatomyReportOverrides &overrides,
    std::string &error);

bool ApplyAnatomyReportOverride(
    const AnatomyConfig &config,
    const AnatomyReportOverrides &overrides,
    AnatomyClassification &classification);

bool ValidateAnatomyConfig(const AnatomyConfig &config, std::string &error);

std::string NormalizeAnatomyLabelText(const std::string &value);

std::vector<std::string> SplitHierarchyPath(const std::string &path);

bool WriteAnatomyReportHeader(std::ostream &out);

bool WriteAnatomyReportRow(std::ostream &out,
                           const AnatomyClassification &classification);

bool UpdateAnatomyAtlasFile(
    const std::string &path,
    const std::vector<AnatomyClassification> &unclassifiedClassifications,
    const std::string &runId,
    size_t &appendedRows,
    std::string &error);
