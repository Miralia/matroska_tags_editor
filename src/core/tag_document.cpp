#include "core/tag_document.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace mte {
namespace {

EditableTarget target_for_tag(const TagTargets& targets) {
  EditableTarget target;
  if (!targets.track_uids.empty()) {
    target.kind = EditableTargetKind::Track;
    target.track_uid = targets.track_uids.front();
  } else if (!targets.edition_uids.empty() || !targets.chapter_uids.empty() ||
             !targets.attachment_uids.empty()) {
    target.kind = EditableTargetKind::Other;
  }
  return target;
}

void add_simple_fields(std::vector<EditableField>& fields,
                       const TagEntry& entry,
                       std::size_t tag_index,
                       const SimpleTag& simple,
                       std::vector<std::size_t> path) {
  if (simple.value_type == TagValueType::String &&
      !is_hidden_extra_tag_name(simple.name)) {
    EditableField field;
    std::ostringstream id;
    id << "tag:" << tag_index;
    for (const auto index : path) {
      id << ":" << index;
    }
    field.id = id.str();
    field.name = simple.name;
    field.value = simple.string_value;
    field.target = target_for_tag(entry.targets);
    field.tag_index = tag_index;
    field.simple_path = std::move(path);
    fields.push_back(std::move(field));
  }

  for (std::size_t i = 0; i < simple.children.size(); ++i) {
    auto child_path = path;
    child_path.push_back(i);
    add_simple_fields(fields, entry, tag_index, simple.children[i], std::move(child_path));
  }
}

}  // namespace

bool TagDocument::empty() const {
  return tags.empty() && !info.title.has_value() && tracks.empty();
}

std::string describe_targets(const TagTargets& targets,
                             const std::vector<TrackInfo>& tracks) {
  if (targets.track_uids.empty() && targets.edition_uids.empty() &&
      targets.chapter_uids.empty() && targets.attachment_uids.empty()) {
    return "Global";
  }

  std::ostringstream label;
  bool first = true;
  const auto append = [&](const std::string& text) {
    if (!first) {
      label << ", ";
    }
    first = false;
    label << text;
  };

  for (const auto uid : targets.track_uids) {
    std::ostringstream item;
    item << "Track " << uid;
    for (const auto& track : tracks) {
      if (track.uid == uid) {
        item << " (#" << track.number;
        if (!track.name.empty()) {
          item << ", " << track.name;
        }
        item << ")";
        break;
      }
    }
    append(item.str());
  }

  for (const auto uid : targets.edition_uids) {
    append("Edition " + std::to_string(uid));
  }
  for (const auto uid : targets.chapter_uids) {
    append("Chapter " + std::to_string(uid));
  }
  for (const auto uid : targets.attachment_uids) {
    append("Attachment " + std::to_string(uid));
  }

  return label.str();
}

bool is_hidden_extra_tag_name(const std::string& name) {
  auto normalized = name;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  return normalized == "BPS" || normalized == "DURATION" ||
         normalized == "NUMBER_OF_FRAMES" || normalized == "NUMBER_OF_BYTES" ||
         normalized == "_STATISTICS_WRITING_APP" ||
         normalized == "_STATISTICS_WRITING_DATE_UTC" ||
         normalized == "_STATISTICS_TAGS";
}

std::vector<EditableField> editable_fields(const TagDocument& document) {
  std::vector<EditableField> fields;
  for (std::size_t tag_index = 0; tag_index < document.tags.size(); ++tag_index) {
    const auto& tag = document.tags[tag_index];
    for (std::size_t simple_index = 0; simple_index < tag.simple_tags.size();
         ++simple_index) {
      add_simple_fields(fields, tag, tag_index, tag.simple_tags[simple_index],
                        {simple_index});
    }
  }

  return fields;
}

}  // namespace mte
