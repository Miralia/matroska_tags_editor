#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mte {

enum class TagValueType {
  String,
  Binary
};

struct SimpleTag {
  std::string name;
  std::string language;
  std::string language_bcp47;
  bool is_default = true;
  TagValueType value_type = TagValueType::String;
  std::string string_value;
  std::vector<std::uint8_t> binary_value;
  std::vector<SimpleTag> children;
};

struct TagTargets {
  std::uint64_t target_type_value = 50;
  std::string target_type;
  std::vector<std::uint64_t> track_uids;
  std::vector<std::uint64_t> edition_uids;
  std::vector<std::uint64_t> chapter_uids;
  std::vector<std::uint64_t> attachment_uids;
};

struct TagEntry {
  TagTargets targets;
  std::vector<SimpleTag> simple_tags;
};

struct TrackInfo {
  std::uint64_t number = 0;
  std::uint64_t uid = 0;
  std::uint64_t type = 0;
  std::string name;
  std::string codec_id;
};

struct TagDocument {
  std::filesystem::path path;
  std::vector<TrackInfo> tracks;
  std::vector<TagEntry> tags;

  bool empty() const;
};

std::string describe_targets(const TagTargets& targets,
                             const std::vector<TrackInfo>& tracks);

}  // namespace mte
