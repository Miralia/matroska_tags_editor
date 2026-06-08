#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mte {

enum class TagValueType {
  String,
  Binary
};

enum class EditableTargetKind {
  General,
  Track,
  Other
};

struct EditableTarget {
  EditableTargetKind kind = EditableTargetKind::General;
  std::uint64_t track_uid = 0;
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

struct SegmentInfo {
  std::optional<std::string> title;
  std::optional<std::string> date_utc;
  std::optional<std::string> muxing_app;
  std::optional<std::string> writing_app;
};

struct TrackInfo {
  std::uint64_t number = 0;
  std::uint64_t uid = 0;
  std::uint64_t original_uid = 0;
  std::uint64_t type = 0;
  std::string name;
  std::string language;
  std::string language_bcp47;
  std::string codec_id;
  std::string codec_name;
  std::string codec_settings;
  std::optional<bool> flag_default;
  std::optional<bool> flag_forced;
  std::optional<bool> flag_enabled;
  std::optional<bool> flag_original;
  std::optional<bool> flag_commentary;
};

struct TagDocument {
  std::filesystem::path path;
  SegmentInfo info;
  std::vector<TrackInfo> tracks;
  std::vector<TagEntry> tags;

  bool empty() const;
};

struct EditableField {
  std::string id;
  std::string name;
  std::string value;
  EditableTarget target;
  std::size_t tag_index = 0;
  std::vector<std::size_t> simple_path;
};

std::string describe_targets(const TagTargets& targets,
                             const std::vector<TrackInfo>& tracks);

bool is_hidden_extra_tag_name(const std::string& name);

std::vector<EditableTarget> editable_targets(const TagDocument& document);

EditableField add_extra_tag(TagDocument& document,
                            EditableTarget target,
                            const std::string& name,
                            const std::string& value);

std::vector<EditableField> editable_fields(const TagDocument& document);

}  // namespace mte
