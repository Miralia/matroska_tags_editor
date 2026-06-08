#include "core/matroska_reader.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "ebml/EbmlBinary.h"
#include "ebml/EbmlElement.h"
#include "ebml/EbmlMaster.h"
#include "ebml/EbmlStream.h"
#include "ebml/EbmlString.h"
#include "ebml/EbmlUInteger.h"
#include "ebml/EbmlUnicodeString.h"
#include "ebml/IOCallback.h"
#include "ebml/StdIOCallback.h"
#include "matroska/KaxContexts.h"
#include "matroska/KaxSegment.h"
#include "matroska/KaxTags.h"
#include "matroska/KaxTrackEntryData.h"
#include "matroska/KaxTracks.h"

namespace mte {
namespace {

using libebml::EbmlBinary;
using libebml::EbmlElement;
using libebml::EbmlMaster;
using libebml::EbmlStream;
using libebml::EbmlString;
using libebml::EbmlUInteger;
using libebml::EbmlUnicodeString;
using libebml::IOCallback;
using libebml::SCOPE_ALL_DATA;
using libebml::StdIOCallback;
using libmatroska::Context_KaxMatroska;
using libmatroska::Context_KaxSegment;
using libmatroska::KaxCodecID;
using libmatroska::KaxSegment;
using libmatroska::KaxTag;
using libmatroska::KaxTagAttachmentUID;
using libmatroska::KaxTagBinary;
using libmatroska::KaxTagChapterUID;
using libmatroska::KaxTagDefault;
using libmatroska::KaxTagEditionUID;
using libmatroska::KaxTagLanguageIETF;
using libmatroska::KaxTagLangue;
using libmatroska::KaxTagName;
using libmatroska::KaxTagSimple;
using libmatroska::KaxTagString;
using libmatroska::KaxTagTargets;
using libmatroska::KaxTagTargetType;
using libmatroska::KaxTagTargetTypeValue;
using libmatroska::KaxTagTrackUID;
using libmatroska::KaxTags;
using libmatroska::KaxTrackEntry;
using libmatroska::KaxTrackName;
using libmatroska::KaxTrackNumber;
using libmatroska::KaxTrackType;
using libmatroska::KaxTrackUID;
using libmatroska::KaxTracks;

constexpr auto kMaxScanSize = std::numeric_limits<uint64>::max();

template <typename T>
const T* first_child(const EbmlMaster& master) {
  return dynamic_cast<const T*>(master.FindFirstElt(EBML_INFO(T)));
}

std::uint64_t uint_value(const EbmlUInteger* element, std::uint64_t fallback = 0) {
  if (!element) {
    return fallback;
  }
  return element->GetValue();
}

std::string ascii_value(const EbmlString* element) {
  if (!element) {
    return {};
  }
  return element->GetValue();
}

std::string utf8_value(const EbmlUnicodeString* element) {
  if (!element) {
    return {};
  }
  return element->GetValueUTF8();
}

std::vector<std::uint8_t> binary_value(const EbmlBinary* element) {
  std::vector<std::uint8_t> value;
  if (!element || !element->GetBuffer() || element->GetSize() == 0) {
    return value;
  }

  const auto* begin = reinterpret_cast<const std::uint8_t*>(element->GetBuffer());
  value.assign(begin, begin + element->GetSize());
  return value;
}

std::vector<std::uint64_t> repeated_uids(const EbmlMaster& master,
                                         const libebml::EbmlCallbacks& callbacks) {
  std::vector<std::uint64_t> values;
  for (const auto* child : master.GetElementList()) {
    if (!child || &child->Generic() != &callbacks) {
      continue;
    }
    if (const auto* integer = dynamic_cast<const EbmlUInteger*>(child)) {
      values.push_back(integer->GetValue());
    }
  }
  return values;
}

TrackInfo parse_track(const KaxTrackEntry& entry) {
  TrackInfo track;
  track.number = uint_value(first_child<KaxTrackNumber>(entry));
  track.uid = uint_value(first_child<KaxTrackUID>(entry));
  track.type = uint_value(first_child<KaxTrackType>(entry));
  track.name = utf8_value(first_child<KaxTrackName>(entry));
  track.codec_id = ascii_value(first_child<KaxCodecID>(entry));
  return track;
}

void parse_tracks(const KaxTracks& tracks_element, TagDocument& document) {
  document.tracks.clear();
  for (const auto* child : tracks_element.GetElementList()) {
    if (const auto* entry = dynamic_cast<const KaxTrackEntry*>(child)) {
      document.tracks.push_back(parse_track(*entry));
    }
  }

  std::sort(document.tracks.begin(), document.tracks.end(),
            [](const TrackInfo& left, const TrackInfo& right) {
              return left.number < right.number;
            });
}

TagTargets parse_targets(const KaxTagTargets& targets_element) {
  TagTargets targets;
  targets.target_type_value =
      uint_value(first_child<KaxTagTargetTypeValue>(targets_element), 50);
  targets.target_type = ascii_value(first_child<KaxTagTargetType>(targets_element));
  targets.track_uids = repeated_uids(targets_element, EBML_INFO(KaxTagTrackUID));
  targets.edition_uids = repeated_uids(targets_element, EBML_INFO(KaxTagEditionUID));
  targets.chapter_uids = repeated_uids(targets_element, EBML_INFO(KaxTagChapterUID));
  targets.attachment_uids =
      repeated_uids(targets_element, EBML_INFO(KaxTagAttachmentUID));
  return targets;
}

SimpleTag parse_simple_tag(const KaxTagSimple& simple_element) {
  SimpleTag tag;
  tag.name = utf8_value(first_child<KaxTagName>(simple_element));
  tag.language = ascii_value(first_child<KaxTagLangue>(simple_element));
  tag.language_bcp47 = ascii_value(first_child<KaxTagLanguageIETF>(simple_element));
  tag.is_default = uint_value(first_child<KaxTagDefault>(simple_element), 1) != 0;

  if (const auto* binary = first_child<KaxTagBinary>(simple_element)) {
    tag.value_type = TagValueType::Binary;
    tag.binary_value = binary_value(binary);
  } else {
    tag.value_type = TagValueType::String;
    tag.string_value = utf8_value(first_child<KaxTagString>(simple_element));
  }

  for (const auto* child : simple_element.GetElementList()) {
    if (const auto* nested = dynamic_cast<const KaxTagSimple*>(child)) {
      tag.children.push_back(parse_simple_tag(*nested));
    }
  }

  return tag;
}

TagEntry parse_tag_entry(const KaxTag& tag_element) {
  TagEntry entry;
  if (const auto* targets = first_child<KaxTagTargets>(tag_element)) {
    entry.targets = parse_targets(*targets);
  }

  for (const auto* child : tag_element.GetElementList()) {
    if (const auto* simple = dynamic_cast<const KaxTagSimple*>(child)) {
      entry.simple_tags.push_back(parse_simple_tag(*simple));
    }
  }
  return entry;
}

void parse_tags(const KaxTags& tags_element, TagDocument& document) {
  document.tags.clear();
  for (const auto* child : tags_element.GetElementList()) {
    if (const auto* tag = dynamic_cast<const KaxTag*>(child)) {
      document.tags.push_back(parse_tag_entry(*tag));
    }
  }
}

void skip_element(IOCallback& input, const EbmlElement& element) {
  if (element.IsFiniteSize()) {
    input.setFilePointer(element.GetEndPosition(), libebml::seek_beginning);
  }
}

void read_segment_children(KaxSegment& segment, IOCallback& input, TagDocument& document) {
  const auto segment_end = segment.IsFiniteSize()
                               ? segment.GetEndPosition()
                               : std::numeric_limits<uint64>::max();
  input.setFilePointer(segment.GetDataStart(), libebml::seek_beginning);
  EbmlStream segment_stream(input);

  while (input.getFilePointer() < segment_end) {
    int upper_level = 0;
    std::unique_ptr<EbmlElement> child(segment_stream.FindNextElement(
        Context_KaxSegment, upper_level, kMaxScanSize, true));
    if (!child || upper_level > 0) {
      break;
    }

    if (auto* tracks = dynamic_cast<KaxTracks*>(child.get())) {
      tracks->ReadData(input, SCOPE_ALL_DATA);
      parse_tracks(*tracks, document);
    } else if (auto* tags = dynamic_cast<KaxTags*>(child.get())) {
      tags->ReadData(input, SCOPE_ALL_DATA);
      parse_tags(*tags, document);
    } else {
      skip_element(input, *child);
    }
  }
}

}  // namespace

TagDocument load_tag_document(const std::filesystem::path& path) {
  TagDocument document;
  document.path = path;

  StdIOCallback input(path.string().c_str(), MODE_READ);
  EbmlStream stream(input);

  while (true) {
    int upper_level = 0;
    std::unique_ptr<EbmlElement> element(
        stream.FindNextElement(Context_KaxMatroska, upper_level, kMaxScanSize, true));
    if (!element) {
      break;
    }

    if (auto* segment = dynamic_cast<KaxSegment*>(element.get())) {
      read_segment_children(*segment, input, document);
      return document;
    }

    skip_element(input, *element);
  }

  throw std::runtime_error("No Matroska segment was found in the file.");
}

}  // namespace mte
