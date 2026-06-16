#include "AnatomyClassifier.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace {
std::string Trim(const std::string &value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string StripInlineComment(const std::string &value) {
  bool inSingleQuote = false;
  bool inDoubleQuote = false;
  for (size_t i = 0; i < value.size(); ++i) {
    const char ch = value[i];
    if (ch == '\'' && !inDoubleQuote) {
      inSingleQuote = !inSingleQuote;
      continue;
    }
    if (ch == '"' && !inSingleQuote) {
      inDoubleQuote = !inDoubleQuote;
      continue;
    }
    if (ch == '#' && !inSingleQuote && !inDoubleQuote) {
      return value.substr(0, i);
    }
  }
  return value;
}

std::string StripQuotes(const std::string &value) {
  const std::string trimmed = Trim(value);
  if (trimmed.size() >= 2) {
    const char first = trimmed.front();
    const char last = trimmed.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      return trimmed.substr(1, trimmed.size() - 2);
    }
  }
  return trimmed;
}

int CountIndent(const std::string &line) {
  int indent = 0;
  while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
    ++indent;
  }
  return indent;
}

bool SplitKeyValue(const std::string &line,
                   std::string &key,
                   std::string &value) {
  const size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  key = Trim(line.substr(0, colon));
  value = StripQuotes(line.substr(colon + 1));
  return !key.empty();
}

std::string CollapseUnderscores(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  bool previousUnderscore = false;
  for (const char ch : value) {
    if (ch == '_') {
      if (!previousUnderscore) {
        out.push_back(ch);
      }
      previousUnderscore = true;
      continue;
    }
    out.push_back(ch);
    previousUnderscore = false;
  }
  while (!out.empty() && out.front() == '_') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  return out;
}

bool StartsWith(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string PadPiece(const std::string &piece) {
  if (piece.empty()) {
    return "000";
  }
  if (piece.size() >= 3) {
    return piece;
  }
  return std::string(3 - piece.size(), '0') + piece;
}

std::string TitleHierarchySegment(const std::string &value) {
  if (value.empty()) {
    return value;
  }
  std::string out;
  out.reserve(value.size());
  bool capitalize = true;
  for (const char ch : value) {
    if (ch == '/' || ch == '_') {
      out.push_back(ch == '/' ? '/' : '_');
      capitalize = true;
      continue;
    }
    if (capitalize) {
      out.push_back(static_cast<char>(
          std::toupper(static_cast<unsigned char>(ch))));
      capitalize = false;
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string SanitizeIdentifierPart(const std::string &value,
                                   const std::string &fallback) {
  std::string out = NormalizeAnatomyLabelText(value);
  std::replace(out.begin(), out.end(), '/', '_');
  if (out.empty()) {
    return fallback;
  }
  return out;
}

void ApplyAlias(AnatomyClassification &classification,
                const AnatomyAlias &alias,
                const std::string &aliasKey) {
  if (!alias.normalizedName.empty()) {
    classification.normalizedName = alias.normalizedName;
  }
  if (!alias.canonicalName.empty()) {
    classification.canonicalName = alias.canonicalName;
  }
  if (!alias.system.empty()) {
    classification.system = alias.system;
  }
  if (!alias.subsystem.empty()) {
    classification.subsystem = alias.subsystem;
  }
  if (!alias.anatomicalRegion.empty()) {
    classification.anatomicalRegion = alias.anatomicalRegion;
  }
  if (!alias.structureType.empty()) {
    classification.structureType = alias.structureType;
  }
  if (!alias.laterality.empty()) {
    classification.laterality = alias.laterality;
  }
  if (!alias.temporalPhase.empty()) {
    classification.temporalPhase = alias.temporalPhase;
  }
  if (!alias.hierarchyPath.empty()) {
    classification.hierarchyPath = alias.hierarchyPath;
  }
  if (!alias.secondarySystems.empty()) {
    classification.secondarySystems = alias.secondarySystems;
  }
  classification.confidence = alias.confidence.empty() ? "exact" : alias.confidence;
  classification.classificationSource =
      classification.classificationSource == "unclassified"
          ? "alias:" + aliasKey
          : classification.classificationSource + ";alias:" + aliasKey;
}

void ApplyRule(AnatomyClassification &classification,
               const AnatomyRule &rule) {
  if (!rule.normalizedName.empty()) {
    classification.normalizedName = rule.normalizedName;
  }
  if (!rule.canonicalName.empty()) {
    classification.canonicalName = rule.canonicalName;
  }
  if (!rule.system.empty()) {
    classification.system = rule.system;
  }
  if (!rule.subsystem.empty()) {
    classification.subsystem = rule.subsystem;
  }
  if (!rule.anatomicalRegion.empty()) {
    classification.anatomicalRegion = rule.anatomicalRegion;
  }
  if (!rule.structureType.empty()) {
    classification.structureType = rule.structureType;
  }
  if (!rule.laterality.empty()) {
    classification.laterality = rule.laterality;
  }
  if (!rule.temporalPhase.empty()) {
    classification.temporalPhase = rule.temporalPhase;
  }
  if (!rule.hierarchyPath.empty()) {
    classification.hierarchyPath = rule.hierarchyPath;
  }
  if (!rule.secondarySystems.empty()) {
    classification.secondarySystems = rule.secondarySystems;
  }
  if (classification.confidence == "unclassified") {
    if (!rule.confidence.empty()) {
      classification.confidence = rule.confidence;
    } else if (rule.priority >= 80) {
      classification.confidence = "high";
    } else if (rule.priority >= 20) {
      classification.confidence = "medium";
    } else {
      classification.confidence = "low";
    }
  }
  classification.classificationSource =
      classification.classificationSource == "unclassified"
          ? "rule:" + rule.id
          : classification.classificationSource + ";rule:" + rule.id;
}

std::string DeriveHierarchyPath(const AnatomyConfig &config,
                                const AnatomyClassification &classification) {
  if (!classification.hierarchyPath.empty() &&
      classification.hierarchyPath != "99_Unclassified") {
    return classification.hierarchyPath;
  }
  const auto root = config.systemRoots.find(classification.system);
  if (root == config.systemRoots.end()) {
    return "99_Unclassified";
  }
  if (classification.subsystem.empty() ||
      classification.subsystem == "unclassified") {
    return root->second;
  }
  return root->second + "/" + TitleHierarchySegment(classification.subsystem);
}

std::string BuildDisplayName(const AnatomyClassification &classification) {
  std::string base = classification.canonicalName.empty()
                         ? classification.normalizedName
                         : classification.canonicalName;
  if (base.empty()) {
    base = "surface";
  }

  if (classification.laterality == "left" && !StartsWith(base, "left_")) {
    base = "left_" + base;
  } else if (classification.laterality == "right" && !StartsWith(base, "right_")) {
    base = "right_" + base;
  }

  if (!classification.pieceNumber.empty()) {
    base += "_" + PadPiece(classification.pieceNumber);
  }

  if (classification.temporalPhase != "static" &&
      classification.temporalPhase != "unspecified" &&
      !StartsWith(base, classification.temporalPhase + "_")) {
    base = classification.temporalPhase + "_" + base;
  }

  return base;
}

std::string BuildCanonicalIdentifier(const AnatomyClassification &classification) {
  std::string id =
      SanitizeIdentifierPart(classification.system, "unclassified") + "__" +
      SanitizeIdentifierPart(classification.subsystem, "unclassified") + "__" +
      SanitizeIdentifierPart(classification.anatomicalRegion, "unspecified") +
      "__" +
      SanitizeIdentifierPart(classification.canonicalName, "unknown") + "__" +
      SanitizeIdentifierPart(classification.laterality, "unspecified") + "__" +
      PadPiece(classification.pieceNumber);
  if (classification.temporalPhase != "static" &&
      classification.temporalPhase != "unspecified") {
    id += "__" + SanitizeIdentifierPart(classification.temporalPhase, "static");
  }
  return id;
}

void ExtractTemporalPrefix(std::string &working,
                           AnatomyClassification &classification) {
  struct Prefix {
    const char *prefix;
    const char *phase;
  };
  const Prefix prefixes[] = {
      {"dias_", "diastole"},
      {"diastole_", "diastole"},
      {"sys_", "systole"},
      {"systole_", "systole"},
  };
  for (const auto &entry : prefixes) {
    if (StartsWith(working, entry.prefix)) {
      classification.temporalPhase = entry.phase;
      working = working.substr(std::string(entry.prefix).size());
      return;
    }
  }
}

void ExtractLaterality(std::string &working,
                       AnatomyClassification &classification) {
  struct Prefix {
    const char *prefix;
    const char *laterality;
  };
  const Prefix prefixes[] = {
      {"left_", "left"},
      {"right_", "right"},
      {"lt_", "left"},
      {"rt_", "right"},
      {"l_", "left"},
      {"r_", "right"},
  };
  for (const auto &entry : prefixes) {
    if (StartsWith(working, entry.prefix)) {
      classification.laterality = entry.laterality;
      working = working.substr(std::string(entry.prefix).size());
      return;
    }
  }

  struct Suffix {
    const char *suffix;
    const char *laterality;
  };
  const Suffix suffixes[] = {
      {"_left", "left"},
      {"_right", "right"},
      {"_lt", "left"},
      {"_rt", "right"},
      {"_l", "left"},
      {"_r", "right"},
  };
  for (const auto &entry : suffixes) {
    if (EndsWith(working, entry.suffix)) {
      classification.laterality = entry.laterality;
      working.resize(working.size() - std::string(entry.suffix).size());
      return;
    }
  }
}

void ExtractPieceNumber(std::string &working,
                        AnatomyClassification &classification) {
  size_t digitBegin = working.size();
  while (digitBegin > 0 &&
         std::isdigit(static_cast<unsigned char>(working[digitBegin - 1]))) {
    --digitBegin;
  }
  if (digitBegin == working.size() || digitBegin == 0) {
    return;
  }

  classification.pieceNumber = working.substr(digitBegin);
  if (digitBegin > 0 && working[digitBegin - 1] == '_') {
    working = working.substr(0, digitBegin - 1);
  } else {
    working = working.substr(0, digitBegin);
  }
  working = CollapseUnderscores(working);
}

void SetAliasField(AnatomyAlias &alias,
                   const std::string &key,
                   const std::string &value) {
  if (key == "normalized_name") {
    alias.normalizedName = value;
  } else if (key == "canonical_name") {
    alias.canonicalName = value;
  } else if (key == "system") {
    alias.system = value;
  } else if (key == "subsystem") {
    alias.subsystem = value;
  } else if (key == "anatomical_region") {
    alias.anatomicalRegion = value;
  } else if (key == "structure_type") {
    alias.structureType = value;
  } else if (key == "laterality") {
    alias.laterality = value;
  } else if (key == "temporal_phase") {
    alias.temporalPhase = value;
  } else if (key == "hierarchy_path") {
    alias.hierarchyPath = value;
  } else if (key == "secondary_systems") {
    alias.secondarySystems = value;
  } else if (key == "confidence") {
    alias.confidence = value;
  }
}

void SetRuleField(AnatomyRule &rule,
                  const std::string &key,
                  const std::string &value) {
  if (key == "id") {
    rule.id = value;
  } else if (key == "priority") {
    try {
      rule.priority = std::stoi(value);
    } catch (const std::exception &) {
      rule.priority = 0;
    }
  } else if (key == "pattern") {
    rule.pattern = value;
  } else if (key == "normalized_name") {
    rule.normalizedName = value;
  } else if (key == "canonical_name") {
    rule.canonicalName = value;
  } else if (key == "system") {
    rule.system = value;
  } else if (key == "subsystem") {
    rule.subsystem = value;
  } else if (key == "anatomical_region") {
    rule.anatomicalRegion = value;
  } else if (key == "structure_type") {
    rule.structureType = value;
  } else if (key == "laterality") {
    rule.laterality = value;
  } else if (key == "temporal_phase") {
    rule.temporalPhase = value;
  } else if (key == "hierarchy_path") {
    rule.hierarchyPath = value;
  } else if (key == "secondary_systems") {
    rule.secondarySystems = value;
  } else if (key == "confidence") {
    rule.confidence = value;
  }
}

AnatomyConfig BaseAnatomyConfig() {
  AnatomyConfig config;
  config.systemRoots = {
      {"fields", "00_Fields"},
      {"cardiovascular", "01_Cardiovascular"},
      {"nervous", "02_Nervous"},
      {"respiratory", "03_Respiratory"},
      {"digestive", "04_Digestive"},
      {"urinary", "05_Urinary"},
      {"reproductive", "06_Reproductive"},
      {"endocrine", "07_Endocrine"},
      {"sensory", "08_Sensory"},
      {"musculoskeletal", "09_Musculoskeletal"},
      {"lymphatic_immune", "10_Lymphatic_and_Immune"},
      {"integumentary", "11_Integumentary"},
      {"unclassified", "99_Unclassified"},
  };
  return config;
}

void AddAlias(AnatomyConfig &config,
              const std::string &name,
              const AnatomyAlias &alias) {
  config.aliases[NormalizeAnatomyLabelText(name)] = alias;
}

void AddRule(AnatomyConfig &config, AnatomyRule rule) {
  if (rule.id.empty()) {
    rule.id = "rule_" + std::to_string(config.rules.size() + 1);
  }
  config.rules.push_back(std::move(rule));
}

AnatomyRule Rule(const std::string &id,
                 int priority,
                 const std::string &pattern,
                 const std::string &system,
                 const std::string &subsystem,
                 const std::string &region,
                 const std::string &type,
                 const std::string &path,
                 const std::string &confidence = std::string(),
                 const std::string &canonical = std::string(),
                 const std::string &secondarySystems = std::string()) {
  AnatomyRule rule;
  rule.id = id;
  rule.priority = priority;
  rule.pattern = pattern;
  rule.system = system;
  rule.subsystem = subsystem;
  rule.anatomicalRegion = region;
  rule.structureType = type;
  rule.hierarchyPath = path;
  rule.confidence = confidence;
  rule.canonicalName = canonical;
  rule.secondarySystems = secondarySystems;
  return rule;
}

std::string CsvEscape(const std::string &value) {
  bool needsQuotes = false;
  for (const char ch : value) {
    if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
      needsQuotes = true;
      break;
    }
  }
  if (!needsQuotes) {
    return value;
  }
  std::string out = "\"";
  for (const char ch : value) {
    if (ch == '"') {
      out += "\"\"";
    } else {
      out.push_back(ch);
    }
  }
  out += "\"";
  return out;
}

std::string NormalizeCsvHeader(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch))) {
      out.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  return out;
}

std::string NormalizeReviewAction(const std::string &value) {
  std::string out = NormalizeAnatomyLabelText(Trim(value));
  std::replace(out.begin(), out.end(), '-', '_');
  return out;
}

bool ShouldLoadOverrideRowForAction(const std::string &reviewAction) {
  const std::string action = NormalizeReviewAction(reviewAction);
  if (action.empty()) {
    return true;
  }
  return action == "corrected" || action == "override" ||
         action == "manual_override" || action == "approved" ||
         action == "accepted" || action == "active";
}

const std::vector<std::string> &AnatomyAtlasHeader() {
  static const std::vector<std::string> header = {
      "original_block_index",
      "original_name",
      "normalized_name",
      "canonical_name",
      "system",
      "subsystem",
      "anatomical_region",
      "structure_type",
      "laterality",
      "piece_number",
      "temporal_phase",
      "hierarchy_path",
      "classification_confidence",
      "classification_rule",
      "source_file",
      "review_action",
      "review_confidence",
      "review_reason",
      "example_source_file",
      "example_original_block_index",
      "first_seen_run_id",
      "notes",
  };
  return header;
}

bool WriteCsvValues(std::ostream &out, const std::vector<std::string> &values) {
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << CsvEscape(values[i]);
  }
  out << '\n';
  return static_cast<bool>(out);
}

std::string AtlasColumnValue(const std::string &column,
                             const AnatomyClassification &classification,
                             const std::string &runId) {
  const std::string key = NormalizeCsvHeader(column);
  if (key == "originalblockindex") {
    return std::string();
  }
  if (key == "originalname") {
    return classification.originalName;
  }
  if (key == "normalizedname") {
    return classification.normalizedName;
  }
  if (key == "canonicalname") {
    return classification.canonicalName;
  }
  if (key == "system") {
    return classification.system;
  }
  if (key == "subsystem") {
    return classification.subsystem;
  }
  if (key == "anatomicalregion") {
    return classification.anatomicalRegion;
  }
  if (key == "structuretype") {
    return classification.structureType;
  }
  if (key == "laterality") {
    return classification.laterality;
  }
  if (key == "piecenumber") {
    return classification.pieceNumber;
  }
  if (key == "temporalphase") {
    return classification.temporalPhase;
  }
  if (key == "hierarchypath") {
    return classification.hierarchyPath;
  }
  if (key == "classificationconfidence") {
    return classification.confidence;
  }
  if (key == "classificationrule") {
    return classification.classificationSource;
  }
  if (key == "sourcefile") {
    return std::string();
  }
  if (key == "reviewaction") {
    return "needs_review";
  }
  if (key == "reviewconfidence") {
    return std::string();
  }
  if (key == "reviewreason") {
    return "Captured unclassified label from an anatomy atlas run; fill the "
           "classification columns and set review_action=corrected to activate.";
  }
  if (key == "examplesourcefile") {
    return classification.sourceFile;
  }
  if (key == "exampleoriginalblockindex") {
    return classification.originalBlockIndex >= 0
               ? std::to_string(classification.originalBlockIndex)
               : std::string();
  }
  if (key == "firstseenrunid") {
    return runId;
  }
  return std::string();
}

bool ParseCsvRecord(const std::string &line,
                    std::vector<std::string> &values,
                    std::string &error) {
  values.clear();
  std::string current;
  bool inQuotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        current.push_back('"');
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }
    if (ch == ',' && !inQuotes) {
      values.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (inQuotes) {
    error = "Unclosed quoted CSV field.";
    return false;
  }
  values.push_back(current);
  return true;
}

std::map<std::string, size_t> BuildCsvHeaderMap(
    const std::vector<std::string> &header) {
  std::map<std::string, size_t> columns;
  for (size_t i = 0; i < header.size(); ++i) {
    const std::string key = NormalizeCsvHeader(header[i]);
    if (!key.empty()) {
      columns[key] = i;
    }
  }
  return columns;
}

bool HasColumn(const std::map<std::string, size_t> &columns,
               const std::vector<std::string> &aliases) {
  for (const std::string &alias : aliases) {
    if (columns.find(NormalizeCsvHeader(alias)) != columns.end()) {
      return true;
    }
  }
  return false;
}

std::string CsvColumnValue(const std::map<std::string, size_t> &columns,
                           const std::vector<std::string> &aliases,
                           const std::vector<std::string> &row) {
  for (const std::string &alias : aliases) {
    const auto found = columns.find(NormalizeCsvHeader(alias));
    if (found == columns.end() || found->second >= row.size()) {
      continue;
    }
    return Trim(row[found->second]);
  }
  return std::string();
}

bool ParseOptionalCsvInt(const std::string &value,
                         int &out,
                         std::string &error) {
  const std::string trimmed = Trim(value);
  if (trimmed.empty()) {
    return false;
  }
  try {
    size_t consumed = 0;
    const int parsed = std::stoi(trimmed, &consumed);
    if (consumed != trimmed.size()) {
      error = "Invalid integer value in anatomy override CSV: " + value;
      return false;
    }
    out = parsed;
    return true;
  } catch (const std::exception &) {
    error = "Invalid integer value in anatomy override CSV: " + value;
    return false;
  }
}

void RefreshDerivedClassificationFields(
    const AnatomyConfig &config,
    AnatomyClassification &classification,
    bool overrideProvidedHierarchyPath) {
  if (classification.system.empty() ||
      classification.system == "unclassified" ||
      (overrideProvidedHierarchyPath &&
       classification.hierarchyPath == "99_Unclassified")) {
    classification.unclassified = true;
    classification.system = "unclassified";
    classification.subsystem = classification.subsystem.empty()
                                   ? "unclassified"
                                   : classification.subsystem;
    if (!overrideProvidedHierarchyPath) {
      classification.hierarchyPath = "99_Unclassified";
    }
    if (classification.confidence.empty()) {
      classification.confidence = "unclassified";
    }
  } else {
    classification.unclassified = false;
    if (!overrideProvidedHierarchyPath) {
      classification.hierarchyPath = DeriveHierarchyPath(config, classification);
    }
    if (classification.confidence.empty() ||
        classification.confidence == "unclassified") {
      classification.confidence = "manual";
    }
  }
  classification.displayName = BuildDisplayName(classification);
  classification.canonicalIdentifier = BuildCanonicalIdentifier(classification);
}
} // namespace

void AnatomySummary::Add(const AnatomyClassification &classification) {
  ++inputBlocks;
  uniqueNormalizedFamilies.insert(classification.normalizedName);
  ++systemCounts[classification.system];
  if (classification.unclassified) {
    ++unclassifiedBlocks;
    unclassifiedLabels.push_back(classification.originalName);
    unclassifiedClassifications.push_back(classification);
  } else {
    ++classifiedBlocks;
  }
}

AnatomyClassifier::AnatomyClassifier(AnatomyConfig config)
    : config_(std::move(config)) {
  std::sort(config_.rules.begin(),
            config_.rules.end(),
            [](const AnatomyRule &left, const AnatomyRule &right) {
              if (left.priority != right.priority) {
                return left.priority > right.priority;
              }
              return left.id < right.id;
            });
}

AnatomyClassification AnatomyClassifier::Classify(
    const std::string &name,
    int originalBlockIndex,
    const std::string &sourceFile) const {
  AnatomyClassification classification;
  classification.originalName = name;
  classification.originalBlockIndex = originalBlockIndex;
  classification.sourceFile = sourceFile;

  const std::string normalizedRaw = NormalizeAnatomyLabelText(name);
  std::string working = normalizedRaw.empty() ? "surface" : normalizedRaw;

  const auto rawAlias = config_.aliases.find(working);
  if (rawAlias != config_.aliases.end()) {
    ApplyAlias(classification, rawAlias->second, rawAlias->first);
    if (!rawAlias->second.normalizedName.empty()) {
      working = NormalizeAnatomyLabelText(rawAlias->second.normalizedName);
    } else if (!rawAlias->second.canonicalName.empty()) {
      working = NormalizeAnatomyLabelText(rawAlias->second.canonicalName);
    }
  }

  ExtractTemporalPrefix(working, classification);
  ExtractLaterality(working, classification);
  ExtractPieceNumber(working, classification);
  if (classification.laterality == "unspecified") {
    ExtractLaterality(working, classification);
  }
  working = CollapseUnderscores(working);
  if (working.empty()) {
    working = normalizedRaw.empty() ? "surface" : normalizedRaw;
  }

  const auto normalizedAlias = config_.aliases.find(working);
  if (normalizedAlias != config_.aliases.end() && normalizedAlias != rawAlias) {
    ApplyAlias(classification, normalizedAlias->second, normalizedAlias->first);
    if (!normalizedAlias->second.normalizedName.empty()) {
      working = NormalizeAnatomyLabelText(normalizedAlias->second.normalizedName);
    } else if (!normalizedAlias->second.canonicalName.empty()) {
      working = NormalizeAnatomyLabelText(normalizedAlias->second.canonicalName);
    }
  }

  if (classification.normalizedName.empty()) {
    classification.normalizedName = working;
  }
  if (classification.canonicalName.empty()) {
    classification.canonicalName = classification.normalizedName;
  }

  const std::string searchText =
      classification.normalizedName + " " + classification.canonicalName + " " +
      normalizedRaw;
  for (const AnatomyRule &rule : config_.rules) {
    if (rule.pattern.empty()) {
      continue;
    }
    try {
      const std::regex pattern(rule.pattern, std::regex::icase);
      if (std::regex_search(searchText, pattern)) {
        ApplyRule(classification, rule);
        break;
      }
    } catch (const std::regex_error &) {
      continue;
    }
  }

  if (classification.system != "unclassified") {
    classification.unclassified = false;
    classification.hierarchyPath = DeriveHierarchyPath(config_, classification);
    if (classification.confidence == "unclassified") {
      classification.confidence = "low";
    }
  } else {
    classification.unclassified = true;
    classification.system = "unclassified";
    classification.subsystem = "unclassified";
    classification.hierarchyPath = "99_Unclassified";
    classification.confidence = "unclassified";
    classification.classificationSource = "unclassified";
  }

  classification.displayName = BuildDisplayName(classification);
  classification.canonicalIdentifier = BuildCanonicalIdentifier(classification);
  return classification;
}

AnatomyConfig DefaultAnatomyConfig() {
  AnatomyConfig config = BaseAnatomyConfig();

  AddAlias(config, "lkidney", AnatomyAlias{"kidney", "kidney", "", "", "", "", "left"});
  AddAlias(config, "rkidney", AnatomyAlias{"kidney", "kidney", "", "", "", "", "right"});
  AddAlias(config, "lv", AnatomyAlias{"left_ventricle", "left_ventricle", "", "", "", "", "left"});
  AddAlias(config, "rv", AnatomyAlias{"right_ventricle", "right_ventricle", "", "", "", "", "right"});
  AddAlias(config, "la", AnatomyAlias{"left_atrium", "left_atrium", "", "", "", "", "left"});
  AddAlias(config, "ra", AnatomyAlias{"right_atrium", "right_atrium", "", "", "", "", "right"});
  AddAlias(config, "lad", AnatomyAlias{"left_anterior_descending_coronary_artery",
                                         "left_anterior_descending_coronary_artery",
                                         "", "", "", "artery", "left"});
  AddAlias(config, "dia", AnatomyAlias{"diaphragm", "diaphragm"});
  AddAlias(config, "pulmonary_arts",
           AnatomyAlias{"pulmonary_artery", "pulmonary_artery"});
  AddAlias(config, "llung", AnatomyAlias{"lung", "lung", "", "", "", "", "left"});
  AddAlias(config, "rlung", AnatomyAlias{"lung", "lung", "", "", "", "", "right"});
  AddAlias(config, "lfoot_musc",
           AnatomyAlias{"foot_muscle", "foot_muscle", "", "", "", "muscle", "left"});
  AddAlias(config, "rfoot_musc",
           AnatomyAlias{"foot_muscle", "foot_muscle", "", "", "", "muscle", "right"});
  AddAlias(config, "dias_lv", AnatomyAlias{"left_ventricle",
                                            "left_ventricle",
                                            "", "", "", "heart", "left",
                                            "diastole"});
  AddAlias(config, "sys_lv", AnatomyAlias{"left_ventricle",
                                           "left_ventricle",
                                           "", "", "", "heart", "left",
                                           "systole"});

  AddRule(config, Rule("pulmonary_artery", 100, ".*pulmonary.*art.*",
                       "cardiovascular", "pulmonary_circulation/arteries",
                       "thorax", "artery",
                       "01_Cardiovascular/Pulmonary_Circulation/Arteries",
                       "high", "pulmonary_artery"));
  AddRule(config, Rule("pulmonary_vein", 100, ".*pulmonary.*vein.*",
                       "cardiovascular", "pulmonary_circulation/veins",
                       "thorax", "vein",
                       "01_Cardiovascular/Pulmonary_Circulation/Veins",
                       "high"));
  AddRule(config, Rule("coronary_artery", 95,
                       ".*(coronary|left_anterior_descending|\\blad\\b|\\brca\\b|\\blcx\\b).*arter.*|^(left_anterior_descending_coronary_artery|lad|rca|lcx)$",
                       "cardiovascular", "coronary_circulation/coronary_arteries",
                       "thorax", "artery",
                       "01_Cardiovascular/Coronary_Circulation/Coronary_Arteries",
                       "high"));
  AddRule(config, Rule("coronary_vein", 95, ".*coronary.*vein.*",
                       "cardiovascular", "coronary_circulation/coronary_veins",
                       "thorax", "vein",
                       "01_Cardiovascular/Coronary_Circulation/Coronary_Veins",
                       "high"));
  AddRule(config, Rule("heart", 90,
                       ".*(heart|atrium|ventricle|myocard|pericard|left_ventricle|right_ventricle|left_atrium|right_atrium).*",
                       "cardiovascular", "heart_and_pericardium", "thorax",
                       "heart", "01_Cardiovascular/Heart_and_Pericardium",
                       "high"));
  AddRule(config, Rule("great_vessels", 88,
                       ".*(aorta|vena_cava|pulmonary_trunk|brachiocephalic).*",
                       "cardiovascular", "great_vessels", "thorax", "other",
                       "01_Cardiovascular/Great_Vessels", "high"));
  AddRule(config, Rule("portal_hepatic_venous", 86,
                       ".*(portal_vein|hepatic_vein).*",
                       "cardiovascular", "portal_and_hepatic_venous_system",
                       "abdomen", "vein",
                       "01_Cardiovascular/Portal_and_Hepatic_Venous_System",
                       "high"));
  AddRule(config, Rule("cerebrovascular_artery", 84,
                       ".*(cerebral|carotid|vertebral|basilar|circle_of_willis).*arter.*",
                       "cardiovascular", "cerebrovascular/arteries",
                       "head_and_neck", "artery",
                       "01_Cardiovascular/Cerebrovascular/Arteries", "high"));
  AddRule(config, Rule("cerebral_artery_segments", 87,
                       ".*(middle_cerebral|posterior_cerebral|posterior_communicating|pericallosal).*",
                       "cardiovascular", "cerebrovascular/arteries",
                       "head_and_neck", "artery",
                       "01_Cardiovascular/Cerebrovascular/Arteries", "high"));
  AddRule(config, Rule("cerebrovascular_vein", 84,
                       ".*(cerebral|dural|sinus|sagittal).*vein.*|.*dural.*sinus.*",
                       "cardiovascular", "cerebrovascular/veins_and_dural_sinuses",
                       "head_and_neck", "vein",
                       "01_Cardiovascular/Cerebrovascular/Veins_and_Dural_Sinuses",
                       "high"));
  AddRule(config, Rule("dural_venous_sinuses", 87,
                       ".*(lateral_sinus|sigmoid_sinus|straight_sinus|superior_saggital_sinus|superior_sagittal_sinus).*",
                       "cardiovascular", "cerebrovascular/veins_and_dural_sinuses",
                       "head_and_neck", "vein",
                       "01_Cardiovascular/Cerebrovascular/Veins_and_Dural_Sinuses",
                       "high"));
  AddRule(config, Rule("generic_artery", 20, ".*(artery|arterial|arter).*",
                       "cardiovascular", "systemic_arteries", "unspecified",
                       "artery", "01_Cardiovascular/Systemic_Arteries",
                       "medium"));
  AddRule(config, Rule("generic_vein", 20, ".*(vein|venous|vena).*",
                       "cardiovascular", "systemic_veins", "unspecified",
                       "vein", "01_Cardiovascular/Systemic_Veins", "medium"));

  AddRule(config, Rule("brain", 90, "^(brain|cerebrum|cortex|white_matter|gray_matter).*",
                       "nervous", "brain", "head_and_neck", "organ",
                       "02_Nervous/Brain", "high"));
  AddRule(config, Rule("brainstem", 90, ".*(brainstem|midbrain|pons|medulla).*",
                       "nervous", "brainstem", "head_and_neck", "nerve",
                       "02_Nervous/Brainstem", "high"));
  AddRule(config, Rule("brainstem_nuclei", 91,
                       ".*(substantia_nigra|superior_colliculus|inferior_colliculus|peria.*grey|peria.*gray|cerebral_peduncle|medullary_pyramid|inferior_olive|tegmentum).*",
                       "nervous", "brainstem", "head_and_neck", "nerve",
                       "02_Nervous/Brainstem", "high"));
  AddRule(config, Rule("deep_brain_structures", 89,
                       ".*(thalamus|putamen|caudate|globus_pallidus|amygdala|hippocampus|mamillary|mammillary|fornix|internal_capsule|anterior_commissure|corpus_callosum).*",
                       "nervous", "brain", "head_and_neck", "organ",
                       "02_Nervous/Brain", "high"));
  AddRule(config, Rule("cortical_brain_regions", 88,
                       ".*(frontobasal|frontal|prefrontal|rolandic|paracentral|parietal|occipital|temporal_lobe|parieto_occipital|mediomedial|posteromedial).*",
                       "nervous", "brain", "head_and_neck", "organ",
                       "02_Nervous/Brain", "medium"));
  AddRule(config, Rule("cerebellum", 90, ".*cerebell.*", "nervous",
                       "cerebellum", "head_and_neck", "organ",
                       "02_Nervous/Cerebellum", "high"));
  AddRule(config, Rule("ventricular_system", 88, ".*(ventricle|csf).*",
                       "nervous", "ventricular_system", "head_and_neck",
                       "fluid", "02_Nervous/Ventricular_System", "medium"));
  AddRule(config, Rule("spinal_cord", 90, ".*spinal_cord.*", "nervous",
                       "spinal_cord", "spine", "nerve",
                       "02_Nervous/Spinal_Cord", "high"));
  AddRule(config, Rule("nerve", 30, ".*nerve.*", "nervous",
                       "peripheral_nerves", "unspecified", "nerve",
                       "02_Nervous/Peripheral_Nerves", "medium"));

  AddRule(config, Rule("lung", 90, ".*\\blung.*", "respiratory", "lungs",
                       "thorax", "organ", "03_Respiratory/Lungs", "high"));
  AddRule(config, Rule("trachea", 90, ".*trachea.*", "respiratory", "trachea",
                       "thorax", "airway", "03_Respiratory/Trachea", "high"));
  AddRule(config, Rule("bronchi", 90, ".*(bronchus|bronchi|bronchial).*",
                       "respiratory", "tracheobronchial_tree/bronchi",
                       "thorax", "airway",
                       "03_Respiratory/Tracheobronchial_Tree/Bronchi",
                       "high", "bronchus"));
  AddRule(config, Rule("pleura", 85, ".*pleura.*", "respiratory", "pleura",
                       "thorax", "other", "03_Respiratory/Pleura", "high"));
  AddRule(config, Rule("upper_airway", 70,
                       ".*(nasal|larynx|laryngeal|pharynx|airway).*",
                       "respiratory", "upper_airway", "head_and_neck", "airway",
                       "03_Respiratory/Upper_Airway", "medium"));
  AddRule(config, Rule("paranasal_sinus", 69, ".*sinus.*",
                       "respiratory", "upper_airway/paranasal_sinuses",
                       "head_and_neck", "cavity",
                       "03_Respiratory/Upper_Airway/Paranasal_Sinuses",
                       "medium"));
  AddRule(config, Rule("laryngeal_soft_tissue", 76,
                       ".*(vocal_folds|thyrohyoid_membrane).*",
                       "respiratory", "upper_airway/larynx",
                       "head_and_neck", "other",
                       "03_Respiratory/Upper_Airway/Larynx", "medium",
                       "", "musculoskeletal"));

  AddRule(config, Rule("teeth", 87, ".*(teeth|tooth).*",
                       "digestive", "oral_cavity", "head_and_neck", "bone",
                       "04_Digestive/Oral_Cavity", "high", "teeth"));
  AddRule(config, Rule("tongue", 87, ".*tongue.*",
                       "digestive", "oral_cavity", "head_and_neck", "organ",
                       "04_Digestive/Oral_Cavity", "high", "",
                       "musculoskeletal"));
  AddRule(config, Rule("oral_cavity", 80, ".*(mouth|oral).*",
                       "digestive", "oral_cavity", "head_and_neck", "cavity",
                       "04_Digestive/Oral_Cavity", "medium"));
  AddRule(config, Rule("salivary_glands", 86,
                       ".*(parotid|subling|submand|salivary).*",
                       "digestive", "oral_cavity/salivary_glands",
                       "head_and_neck", "gland",
                       "04_Digestive/Oral_Cavity/Salivary_Glands", "high"));
  AddRule(config, Rule("esophagus", 85, ".*(esophagus|oesophagus|pharynx).*",
                       "digestive", "pharynx_and_esophagus", "thorax", "organ",
                       "04_Digestive/Pharynx_and_Esophagus", "medium"));
  AddRule(config, Rule("stomach", 90, ".*stomach.*", "digestive", "stomach",
                       "abdomen", "organ", "04_Digestive/Stomach", "high"));
  AddRule(config, Rule("small_intestine", 85,
                       ".*(small_intestine|small_intest|sm_intest|duodenum|jejunum|ileum).*",
                       "digestive", "small_intestine", "abdomen", "organ",
                       "04_Digestive/Small_Intestine", "high"));
  AddRule(config, Rule("large_intestine", 85,
                       ".*(large_intestine|large_intest|large_int|trans_large_int|sigmoid|colon|cecum|rectum|appendix).*",
                       "digestive", "large_intestine", "abdomen", "organ",
                       "04_Digestive/Large_Intestine", "high"));
  AddRule(config, Rule("liver", 90, ".*liver.*", "digestive", "liver",
                       "abdomen", "organ", "04_Digestive/Liver", "high"));
  AddRule(config, Rule("biliary", 85, ".*(gallbladder|bile|biliary).*",
                       "digestive", "biliary_system", "abdomen", "duct",
                       "04_Digestive/Biliary_System", "high"));
  AddRule(config, Rule("pancreas_digestive", 80, ".*pancreas.*",
                       "digestive", "pancreas", "abdomen", "gland",
                       "04_Digestive/Pancreas", "medium", "", "endocrine"));

  AddRule(config, Rule("kidney", 92, ".*kidney.*", "urinary", "kidneys",
                       "abdomen", "organ", "05_Urinary/Kidneys", "high"));
  AddRule(config, Rule("renal_collecting", 85,
                       ".*(renal_pelvis|calyx|calyces|collecting).*",
                       "urinary", "renal_collecting_system", "abdomen", "duct",
                       "05_Urinary/Renal_Collecting_System", "high"));
  AddRule(config, Rule("ureter", 85, ".*ureter.*", "urinary", "ureters",
                       "abdomen", "duct", "05_Urinary/Ureters", "high"));
  AddRule(config, Rule("bladder", 85, ".*(urinary_bladder|bladder).*",
                       "urinary", "urinary_bladder", "pelvis", "organ",
                       "05_Urinary/Urinary_Bladder", "high"));
  AddRule(config, Rule("urethra", 85, ".*urethra.*", "urinary", "urethra",
                       "pelvis", "duct", "05_Urinary/Urethra", "high"));

  AddRule(config, Rule("male_reproductive", 80,
                       ".*(prostate|testis|testicle|seminal|penis|scrotum|epididym|vas_def|vas_deferens|ejaculatory).*",
                       "reproductive", "male", "pelvis", "organ",
                       "06_Reproductive/Male", "medium"));
  AddRule(config, Rule("female_reproductive", 80,
                       ".*(ovary|uterus|uterine|vagina|fallopian|cervix).*",
                       "reproductive", "female", "pelvis", "organ",
                       "06_Reproductive/Female", "medium"));

  AddRule(config, Rule("pituitary", 90, ".*pituitary.*", "endocrine",
                       "pituitary", "head_and_neck", "gland",
                       "07_Endocrine/Pituitary", "high"));
  AddRule(config, Rule("pineal", 90, ".*pineal.*", "endocrine",
                       "other/pineal", "head_and_neck", "gland",
                       "07_Endocrine/Other/Pineal", "high", "",
                       "nervous"));
  AddRule(config, Rule("thyroid", 90, ".*thyroid.*", "endocrine", "thyroid",
                       "head_and_neck", "gland", "07_Endocrine/Thyroid",
                       "high"));
  AddRule(config, Rule("parathyroid", 90, ".*parathyroid.*", "endocrine",
                       "parathyroid", "head_and_neck", "gland",
                       "07_Endocrine/Parathyroid", "high"));
  AddRule(config, Rule("adrenal", 90, ".*adrenal.*", "endocrine", "adrenal",
                       "abdomen", "gland", "07_Endocrine/Adrenal", "high",
                       "", "urinary"));

  AddRule(config, Rule("visual_system", 85,
                       ".*(eye|retina|lens|cornea|sclera|optic).*",
                       "sensory", "visual_system", "head_and_neck", "organ",
                       "08_Sensory/Visual_System", "medium"));
  AddRule(config, Rule("auditory_system", 85,
                       ".*(ear|cochlea|auditory).*", "sensory",
                       "auditory_system", "head_and_neck", "organ",
                       "08_Sensory/Auditory_System", "medium"));
  AddRule(config, Rule("olfactory_system", 85, ".*olfactory.*", "sensory",
                       "olfactory_system", "head_and_neck", "nerve",
                       "08_Sensory/Olfactory_System", "medium"));
  AddRule(config, Rule("vestibular_system", 85, ".*vestibular.*", "sensory",
                       "vestibular_system", "head_and_neck", "organ",
                       "08_Sensory/Vestibular_System", "medium"));

  AddRule(config, Rule("skull", 90, ".*(skull|cranium|mandible|maxilla).*",
                       "musculoskeletal", "skeleton/skull", "head_and_neck",
                       "bone", "09_Musculoskeletal/Skeleton/Skull", "high"));
  AddRule(config, Rule("vertebral_column", 90,
                       ".*(vertebra|cervical|thoracic|lumbar|sacrum|coccyx).*",
                       "musculoskeletal", "skeleton/vertebral_column", "spine",
                       "bone", "09_Musculoskeletal/Skeleton/Vertebral_Column",
                       "high"));
  AddRule(config, Rule("thoracic_cage", 88, ".*(rib|sternum).*",
                       "musculoskeletal", "skeleton/thoracic_cage", "thorax",
                       "bone", "09_Musculoskeletal/Skeleton/Thoracic_Cage",
                       "high"));
  AddRule(config, Rule("shoulder_girdle", 86, ".*(scapula|clavicle).*",
                       "musculoskeletal", "skeleton/shoulder_girdle",
                       "upper_limb", "bone",
                       "09_Musculoskeletal/Skeleton/Shoulder_Girdle", "high"));
  AddRule(config, Rule("upper_limb_bone", 86,
                       ".*(humerus|radius|ulna|carpal|metacarpal|capitate|hamate|lunate|pisform|pisiform|scaphoid|trapezium|trapezoid|triquetrum).*",
                       "musculoskeletal", "skeleton/upper_limb", "upper_limb",
                       "bone", "09_Musculoskeletal/Skeleton/Upper_Limb",
                       "high"));
  AddRule(config, Rule("pelvis_bone", 86, ".*(pelvis|ilium|ischium|pubis|hip).*",
                       "musculoskeletal", "skeleton/pelvis", "pelvis", "bone",
                       "09_Musculoskeletal/Skeleton/Pelvis", "high"));
  AddRule(config, Rule("lower_limb_bone", 86,
                       ".*(femur|tibia|fibula|patella|tarsal|metatarsal|calcaneus|talus|cuboid|navicular).*",
                       "musculoskeletal", "skeleton/lower_limb", "lower_limb",
                       "bone", "09_Musculoskeletal/Skeleton/Lower_Limb",
                       "high"));
  AddRule(config, Rule("phalanges", 86, ".*phalanx.*|.*phalange.*",
                       "musculoskeletal", "skeleton/phalanges", "unspecified",
                       "bone", "09_Musculoskeletal/Skeleton/Phalanges",
                       "medium", "phalanx"));
  AddRule(config, Rule("hand_digits", 84, ".*(thumb|pinky|ring|middle).*",
                       "musculoskeletal", "skeleton/upper_limb", "upper_limb",
                       "bone", "09_Musculoskeletal/Skeleton/Upper_Limb",
                       "medium"));
  AddRule(config, Rule("diaphragm", 82, ".*diaphragm.*|^dia$",
                       "musculoskeletal", "skeletal_muscle", "thorax",
                       "muscle", "09_Musculoskeletal/Skeletal_Muscle",
                       "medium", "diaphragm", "respiratory"));
  AddRule(config, Rule("lower_limb_soft_tissue", 72,
                       ".*(leg|foot_muscle).*",
                       "musculoskeletal", "skeletal_muscle/lower_limb",
                       "lower_limb", "muscle",
                       "09_Musculoskeletal/Skeletal_Muscle/Lower_Limb",
                       "medium"));
  AddRule(config, Rule("intervertebral_disc", 85, ".*disc.*",
                       "musculoskeletal", "intervertebral_discs", "spine",
                       "cartilage", "09_Musculoskeletal/Intervertebral_Discs",
                       "medium"));
  AddRule(config, Rule("cartilage", 75, ".*cartilage.*", "musculoskeletal",
                       "cartilage", "unspecified", "cartilage",
                       "09_Musculoskeletal/Cartilage", "medium"));
  AddRule(config, Rule("ligament", 75, ".*ligament.*", "musculoskeletal",
                       "ligaments", "unspecified", "ligament",
                       "09_Musculoskeletal/Ligaments", "medium"));
  AddRule(config, Rule("tendon", 75, ".*tendon.*", "musculoskeletal",
                       "tendons", "unspecified", "tendon",
                       "09_Musculoskeletal/Tendons", "medium"));
  AddRule(config, Rule("muscle", 70, ".*(muscle|musculus|musc).*",
                       "musculoskeletal", "skeletal_muscle", "unspecified",
                       "muscle", "09_Musculoskeletal/Skeletal_Muscle",
                       "medium", "skeletal_muscle"));
  AddRule(config, Rule("generic_bone", 25, ".*bone.*", "musculoskeletal",
                       "skeleton", "unspecified", "bone",
                       "09_Musculoskeletal/Skeleton", "medium"));

  AddRule(config, Rule("lymph_node", 90, ".*lymph.*node.*",
                       "lymphatic_immune", "lymph_nodes", "unspecified",
                       "organ", "10_Lymphatic_and_Immune/Lymph_Nodes",
                       "high"));
  AddRule(config, Rule("lymphatic_vessel", 85, ".*lymph.*vessel.*",
                       "lymphatic_immune", "lymphatic_vessels", "unspecified",
                       "duct", "10_Lymphatic_and_Immune/Lymphatic_Vessels",
                       "medium"));
  AddRule(config, Rule("thymus", 90, ".*thymus.*", "lymphatic_immune",
                       "thymus", "thorax", "organ",
                       "10_Lymphatic_and_Immune/Thymus", "high"));
  AddRule(config, Rule("spleen", 90, ".*spleen.*", "lymphatic_immune",
                       "spleen", "abdomen", "organ",
                       "10_Lymphatic_and_Immune/Spleen", "high", "",
                       "digestive"));
  AddRule(config, Rule("tonsil", 85, ".*tonsil.*", "lymphatic_immune",
                       "tonsils", "head_and_neck", "organ",
                       "10_Lymphatic_and_Immune/Tonsils", "medium"));
  AddRule(config, Rule("bone_marrow", 85, ".*marrow.*", "lymphatic_immune",
                       "bone_marrow", "unspecified", "organ",
                       "10_Lymphatic_and_Immune/Bone_Marrow", "medium",
                       "", "musculoskeletal"));

  AddRule(config, Rule("skin", 90, ".*skin.*", "integumentary", "skin",
                       "whole_body", "organ", "11_Integumentary/Skin",
                       "high"));
  AddRule(config, Rule("subcutaneous", 70, ".*(subcutaneous|adipose|fat).*",
                       "integumentary", "subcutaneous_tissue", "whole_body",
                       "other", "11_Integumentary/Subcutaneous_Tissue",
                       "medium"));

  return config;
}

bool LoadAnatomyConfigFile(
    const std::string &path,
    AnatomyConfig &config,
    std::string &error) {
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Cannot open anatomy config: " + path;
    return false;
  }

  enum class Section { None, Aliases, Rules };
  Section section = Section::None;
  std::string currentAliasKey;
  AnatomyRule *currentRule = nullptr;

  std::string rawLine;
  int lineNumber = 0;
  while (std::getline(file, rawLine)) {
    ++lineNumber;
    const std::string withoutComment = StripInlineComment(rawLine);
    const std::string line = Trim(withoutComment);
    if (line.empty()) {
      continue;
    }

    if (line == "aliases:") {
      section = Section::Aliases;
      currentAliasKey.clear();
      currentRule = nullptr;
      continue;
    }
    if (line == "rules:") {
      section = Section::Rules;
      currentAliasKey.clear();
      currentRule = nullptr;
      continue;
    }

    const int indent = CountIndent(withoutComment);
    if (section == Section::Aliases) {
      if (indent == 2 && EndsWith(line, ":")) {
        currentAliasKey =
            NormalizeAnatomyLabelText(line.substr(0, line.size() - 1));
        config.aliases[currentAliasKey] = AnatomyAlias{};
        continue;
      }
      std::string key;
      std::string value;
      if (indent >= 4 && !currentAliasKey.empty() &&
          SplitKeyValue(line, key, value)) {
        SetAliasField(config.aliases[currentAliasKey], key, value);
        continue;
      }
    } else if (section == Section::Rules) {
      if (indent == 2 && StartsWith(line, "- ")) {
        config.rules.push_back(AnatomyRule{});
        currentRule = &config.rules.back();
        const std::string rest = Trim(line.substr(2));
        if (!rest.empty()) {
          std::string key;
          std::string value;
          if (SplitKeyValue(rest, key, value)) {
            SetRuleField(*currentRule, key, value);
          }
        }
        continue;
      }
      std::string key;
      std::string value;
      if (indent >= 4 && currentRule && SplitKeyValue(line, key, value)) {
        SetRuleField(*currentRule, key, value);
        continue;
      }
    }

    error = "Unsupported anatomy config line " + std::to_string(lineNumber) +
            " in " + path + ": " + rawLine;
    return false;
  }

  for (size_t i = 0; i < config.rules.size(); ++i) {
    if (config.rules[i].id.empty()) {
      config.rules[i].id = "rule_" + std::to_string(i + 1);
    }
  }
  return true;
}

bool LoadDefaultOrConfiguredAnatomyConfig(
    const std::string &path,
    AnatomyConfig &config,
    std::string &error) {
  if (!path.empty()) {
    config = DefaultAnatomyConfig();
    if (!LoadAnatomyConfigFile(path, config, error)) {
      return false;
    }
    return ValidateAnatomyConfig(config, error);
  }

  const std::filesystem::path relativeConfig =
      std::filesystem::path("config") / "anatomy_hierarchy.yml";

  std::vector<std::filesystem::path> configCandidates;
  configCandidates.push_back(relativeConfig);

  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    std::filesystem::path probe = cwd;
    for (int i = 0; i < 8; ++i) {
      configCandidates.push_back(probe / relativeConfig);
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

  for (const auto &candidate : configCandidates) {
    if (!std::filesystem::exists(candidate)) {
      continue;
    }
    config = BaseAnatomyConfig();
    if (!LoadAnatomyConfigFile(candidate.string(), config, error)) {
      return false;
    }
    return ValidateAnatomyConfig(config, error);
  }

  config = DefaultAnatomyConfig();
  return ValidateAnatomyConfig(config, error);
}

bool LoadAnatomyReportOverrides(
    const std::string &path,
    AnatomyReportOverrides &overrides,
    std::string &error) {
  overrides = AnatomyReportOverrides{};
  std::ifstream file(path);
  if (!file.is_open()) {
    error = "Cannot open anatomy override CSV: " + path;
    return false;
  }

  std::string rawLine;
  int lineNumber = 0;
  std::vector<std::string> header;
  while (std::getline(file, rawLine)) {
    ++lineNumber;
    if (Trim(rawLine).empty()) {
      continue;
    }
    if (!ParseCsvRecord(rawLine, header, error)) {
      error = "Invalid anatomy override CSV header in " + path + ": " + error;
      return false;
    }
    break;
  }
  if (header.empty()) {
    error = "Anatomy override CSV is empty: " + path;
    return false;
  }

  const std::map<std::string, size_t> columns = BuildCsvHeaderMap(header);
  const std::vector<std::string> originalIndexColumns = {
      "original_block_index", "OriginalBlockIndex"};
  const std::vector<std::string> originalNameColumns = {
      "original_name", "OriginalBlockName"};
  if (!HasColumn(columns, originalNameColumns)) {
    error = "Anatomy override CSV must include original_name or OriginalBlockName.";
    return false;
  }

  overrides.columns.normalizedName =
      HasColumn(columns, {"normalized_name", "NormalizedAnatomyName"});
  overrides.columns.canonicalName =
      HasColumn(columns, {"canonical_name", "CanonicalAnatomyName"});
  overrides.columns.system =
      HasColumn(columns, {"system", "AnatomicalSystem"});
  overrides.columns.subsystem =
      HasColumn(columns, {"subsystem", "AnatomicalSubsystem"});
  overrides.columns.anatomicalRegion =
      HasColumn(columns, {"anatomical_region", "AnatomicalRegion"});
  overrides.columns.structureType =
      HasColumn(columns, {"structure_type", "StructureType"});
  overrides.columns.laterality =
      HasColumn(columns, {"laterality", "Laterality"});
  overrides.columns.pieceNumber =
      HasColumn(columns, {"piece_number", "PieceNumber"});
  overrides.columns.temporalPhase =
      HasColumn(columns, {"temporal_phase", "TemporalPhase"});
  overrides.columns.hierarchyPath =
      HasColumn(columns, {"hierarchy_path", "HierarchyPath"});
  overrides.columns.confidence =
      HasColumn(columns, {"classification_confidence",
                          "ClassificationConfidence"});
  overrides.columns.classificationSource =
      HasColumn(columns, {"classification_rule", "ClassificationRule"});
  overrides.columns.sourceFile =
      HasColumn(columns, {"source_file", "SourceFile"});
  const std::vector<std::string> reviewActionColumns = {
      "review_action", "ReviewAction", "atlas_status", "AtlasStatus"};

  std::vector<std::string> row;
  while (std::getline(file, rawLine)) {
    ++lineNumber;
    if (Trim(rawLine).empty()) {
      continue;
    }
    if (!ParseCsvRecord(rawLine, row, error)) {
      error = "Invalid anatomy override CSV line " +
              std::to_string(lineNumber) + " in " + path + ": " + error;
      return false;
    }

    AnatomyClassification classification;
    classification.originalName =
        CsvColumnValue(columns, originalNameColumns, row);
    if (classification.originalName.empty()) {
      error = "Anatomy override CSV line " + std::to_string(lineNumber) +
              " is missing original_name.";
      return false;
    }

    const std::string reviewAction =
        CsvColumnValue(columns, reviewActionColumns, row);
    if (!ShouldLoadOverrideRowForAction(reviewAction)) {
      ++overrides.skippedRows;
      continue;
    }

    bool hasOriginalIndex = false;
    const std::string originalIndexText =
        CsvColumnValue(columns, originalIndexColumns, row);
    if (!originalIndexText.empty()) {
      hasOriginalIndex = ParseOptionalCsvInt(originalIndexText,
                                             classification.originalBlockIndex,
                                             error);
      if (!hasOriginalIndex) {
        error = "Anatomy override CSV line " + std::to_string(lineNumber) +
                ": " + error;
        return false;
      }
    }

    if (overrides.columns.normalizedName) {
      classification.normalizedName =
          CsvColumnValue(columns, {"normalized_name", "NormalizedAnatomyName"}, row);
    }
    if (overrides.columns.canonicalName) {
      classification.canonicalName =
          CsvColumnValue(columns, {"canonical_name", "CanonicalAnatomyName"}, row);
    }
    if (overrides.columns.system) {
      classification.system =
          CsvColumnValue(columns, {"system", "AnatomicalSystem"}, row);
    }
    if (overrides.columns.subsystem) {
      classification.subsystem =
          CsvColumnValue(columns, {"subsystem", "AnatomicalSubsystem"}, row);
    }
    if (overrides.columns.anatomicalRegion) {
      classification.anatomicalRegion =
          CsvColumnValue(columns, {"anatomical_region", "AnatomicalRegion"}, row);
    }
    if (overrides.columns.structureType) {
      classification.structureType =
          CsvColumnValue(columns, {"structure_type", "StructureType"}, row);
    }
    if (overrides.columns.laterality) {
      classification.laterality =
          CsvColumnValue(columns, {"laterality", "Laterality"}, row);
    }
    if (overrides.columns.pieceNumber) {
      classification.pieceNumber =
          CsvColumnValue(columns, {"piece_number", "PieceNumber"}, row);
    }
    if (overrides.columns.temporalPhase) {
      classification.temporalPhase =
          CsvColumnValue(columns, {"temporal_phase", "TemporalPhase"}, row);
    }
    if (overrides.columns.hierarchyPath) {
      classification.hierarchyPath =
          CsvColumnValue(columns, {"hierarchy_path", "HierarchyPath"}, row);
    }
    if (overrides.columns.confidence) {
      classification.confidence =
          CsvColumnValue(columns,
                         {"classification_confidence",
                          "ClassificationConfidence"},
                         row);
    }
    if (overrides.columns.classificationSource) {
      classification.classificationSource =
          CsvColumnValue(columns, {"classification_rule", "ClassificationRule"}, row);
    }
    if (overrides.columns.sourceFile) {
      classification.sourceFile =
          CsvColumnValue(columns, {"source_file", "SourceFile"}, row);
    }

    if (hasOriginalIndex) {
      if (!classification.sourceFile.empty()) {
        overrides.bySourceIndexAndName[{classification.sourceFile,
                                        classification.originalBlockIndex,
                                        classification.originalName}] =
            classification;
      }
      overrides.byIndexAndName[{classification.originalBlockIndex,
                                classification.originalName}] = classification;
    } else {
      overrides.byOriginalName[classification.originalName] = classification;
    }
    ++overrides.rowCount;
  }

  return true;
}

bool ApplyAnatomyReportOverride(
    const AnatomyConfig &config,
    const AnatomyReportOverrides &overrides,
    AnatomyClassification &classification) {
  const AnatomyClassification *overrideClassification = nullptr;
  const auto sourceExact = overrides.bySourceIndexAndName.find(
      {classification.sourceFile,
       classification.originalBlockIndex,
       classification.originalName});
  if (sourceExact != overrides.bySourceIndexAndName.end()) {
    overrideClassification = &sourceExact->second;
  }

  const auto exact = overrides.byIndexAndName.find(
      {classification.originalBlockIndex, classification.originalName});
  if (!overrideClassification && exact != overrides.byIndexAndName.end()) {
    overrideClassification = &exact->second;
  }

  if (!overrideClassification) {
    const auto byName = overrides.byOriginalName.find(classification.originalName);
    if (byName != overrides.byOriginalName.end()) {
      overrideClassification = &byName->second;
    }
  }

  if (!overrideClassification) {
    return false;
  }

  const AnatomyReportOverrideColumns &columns = overrides.columns;
  if (columns.normalizedName) {
    classification.normalizedName = overrideClassification->normalizedName;
  }
  if (columns.canonicalName) {
    classification.canonicalName = overrideClassification->canonicalName;
  }
  if (columns.system) {
    classification.system = overrideClassification->system;
  }
  if (columns.subsystem) {
    classification.subsystem = overrideClassification->subsystem;
  }
  if (columns.anatomicalRegion) {
    classification.anatomicalRegion = overrideClassification->anatomicalRegion;
  }
  if (columns.structureType) {
    classification.structureType = overrideClassification->structureType;
  }
  if (columns.laterality) {
    classification.laterality = overrideClassification->laterality;
  }
  if (columns.pieceNumber) {
    classification.pieceNumber = overrideClassification->pieceNumber;
  }
  if (columns.temporalPhase) {
    classification.temporalPhase = overrideClassification->temporalPhase;
  }
  if (columns.hierarchyPath) {
    classification.hierarchyPath = overrideClassification->hierarchyPath;
  }
  if (columns.confidence) {
    classification.confidence = overrideClassification->confidence;
  }
  if (columns.sourceFile) {
    classification.sourceFile = overrideClassification->sourceFile;
  }

  if (columns.classificationSource &&
      !overrideClassification->classificationSource.empty()) {
    classification.classificationSource =
        "report_override:" + overrideClassification->classificationSource;
  } else {
    classification.classificationSource = "report_override";
  }

  RefreshDerivedClassificationFields(config,
                                     classification,
                                     columns.hierarchyPath);
  return true;
}

bool ValidateAnatomyConfig(const AnatomyConfig &config, std::string &error) {
  for (const AnatomyRule &rule : config.rules) {
    if (rule.pattern.empty()) {
      error = "Anatomy rule has no pattern: " + rule.id;
      return false;
    }
    try {
      const std::regex pattern(rule.pattern, std::regex::icase);
      (void)pattern;
    } catch (const std::regex_error &ex) {
      error = "Invalid regex in anatomy rule '" + rule.id + "': " + ex.what();
      return false;
    }
  }
  return true;
}

std::string NormalizeAnatomyLabelText(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  bool previousUnderscore = false;
  const std::string trimmed = Trim(value);
  for (const unsigned char ch : trimmed) {
    char normalized = static_cast<char>(std::tolower(ch));
    if (normalized == '-' || std::isspace(ch)) {
      normalized = '_';
    }
    if (normalized == '_') {
      if (!previousUnderscore) {
        out.push_back('_');
      }
      previousUnderscore = true;
      continue;
    }
    out.push_back(normalized);
    previousUnderscore = false;
  }
  return CollapseUnderscores(out);
}

std::vector<std::string> SplitHierarchyPath(const std::string &path) {
  std::vector<std::string> parts;
  std::stringstream stream(path);
  std::string part;
  while (std::getline(stream, part, '/')) {
    part = Trim(part);
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  if (parts.empty()) {
    parts.push_back("99_Unclassified");
  }
  return parts;
}

bool WriteAnatomyReportHeader(std::ostream &out) {
  out << "original_block_index,original_name,normalized_name,canonical_name,"
         "system,subsystem,anatomical_region,structure_type,laterality,"
         "piece_number,temporal_phase,hierarchy_path,classification_confidence,"
         "classification_rule,source_file\n";
  return static_cast<bool>(out);
}

bool WriteAnatomyReportRow(std::ostream &out,
                           const AnatomyClassification &classification) {
  out << classification.originalBlockIndex << ','
      << CsvEscape(classification.originalName) << ','
      << CsvEscape(classification.normalizedName) << ','
      << CsvEscape(classification.canonicalName) << ','
      << CsvEscape(classification.system) << ','
      << CsvEscape(classification.subsystem) << ','
      << CsvEscape(classification.anatomicalRegion) << ','
      << CsvEscape(classification.structureType) << ','
      << CsvEscape(classification.laterality) << ','
      << CsvEscape(classification.pieceNumber) << ','
      << CsvEscape(classification.temporalPhase) << ','
      << CsvEscape(classification.hierarchyPath) << ','
      << CsvEscape(classification.confidence) << ','
      << CsvEscape(classification.classificationSource) << ','
      << CsvEscape(classification.sourceFile) << '\n';
  return static_cast<bool>(out);
}

bool UpdateAnatomyAtlasFile(
    const std::string &path,
    const std::vector<AnatomyClassification> &unclassifiedClassifications,
    const std::string &runId,
    size_t &appendedRows,
    std::string &error) {
  appendedRows = 0;
  if (path.empty()) {
    error = "Missing anatomy atlas path.";
    return false;
  }

  std::vector<std::string> header;
  std::set<std::string> existingNames;
  const std::filesystem::path atlasPath(path);
  if (std::filesystem::exists(atlasPath)) {
    std::ifstream in(atlasPath);
    if (!in.is_open()) {
      error = "Cannot read anatomy atlas CSV: " + path;
      return false;
    }

    std::string rawLine;
    int lineNumber = 0;
    while (std::getline(in, rawLine)) {
      ++lineNumber;
      if (Trim(rawLine).empty()) {
        continue;
      }
      if (!ParseCsvRecord(rawLine, header, error)) {
        error = "Invalid anatomy atlas CSV header in " + path + ": " + error;
        return false;
      }
      break;
    }

    if (header.empty()) {
      header = AnatomyAtlasHeader();
    }

    const std::map<std::string, size_t> columns = BuildCsvHeaderMap(header);
    if (!HasColumn(columns, {"original_name", "OriginalBlockName"})) {
      error = "Anatomy atlas CSV must include original_name.";
      return false;
    }
    if (!HasColumn(columns, {"review_action", "ReviewAction", "atlas_status",
                             "AtlasStatus"})) {
      error = "Anatomy atlas CSV must include review_action so pending rows "
              "are not applied as overrides.";
      return false;
    }

    std::vector<std::string> row;
    while (std::getline(in, rawLine)) {
      ++lineNumber;
      if (Trim(rawLine).empty()) {
        continue;
      }
      if (!ParseCsvRecord(rawLine, row, error)) {
        error = "Invalid anatomy atlas CSV line " +
                std::to_string(lineNumber) + " in " + path + ": " + error;
        return false;
      }
      const std::string originalName =
          CsvColumnValue(columns, {"original_name", "OriginalBlockName"}, row);
      if (!originalName.empty()) {
        existingNames.insert(NormalizeAnatomyLabelText(originalName));
      }
    }
  } else {
    header = AnatomyAtlasHeader();
  }

  const std::filesystem::path parent = atlasPath.parent_path();
  if (!parent.empty()) {
    std::error_code fsError;
    std::filesystem::create_directories(parent, fsError);
    if (fsError) {
      error = "Failed to create anatomy atlas directory: " + parent.string();
      return false;
    }
  }

  const bool needsHeader = !std::filesystem::exists(atlasPath) ||
                           std::filesystem::file_size(atlasPath) == 0;
  std::ofstream out(atlasPath, std::ios::app);
  if (!out.is_open()) {
    error = "Cannot write anatomy atlas CSV: " + path;
    return false;
  }

  if (needsHeader) {
    if (!WriteCsvValues(out, header)) {
      error = "Failed to write anatomy atlas header: " + path;
      return false;
    }
  }

  for (const AnatomyClassification &classification :
       unclassifiedClassifications) {
    if (classification.originalName.empty()) {
      continue;
    }
    const std::string key =
        NormalizeAnatomyLabelText(classification.originalName);
    if (key.empty() || existingNames.find(key) != existingNames.end()) {
      continue;
    }

    std::vector<std::string> values;
    values.reserve(header.size());
    for (const std::string &column : header) {
      values.push_back(AtlasColumnValue(column, classification, runId));
    }
    if (!WriteCsvValues(out, values)) {
      error = "Failed to append anatomy atlas row for " +
              classification.originalName;
      return false;
    }
    existingNames.insert(key);
    ++appendedRows;
  }

  return true;
}
