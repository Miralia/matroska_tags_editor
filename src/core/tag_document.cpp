#include "core/tag_document.h"

#include <sstream>

namespace mte {

bool TagDocument::empty() const {
  return tags.empty();
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

}  // namespace mte
