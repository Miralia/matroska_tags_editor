#include "core/matroska_reader.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mte {
namespace {

constexpr std::uint64_t kSegment = 0x18538067;
constexpr std::uint64_t kTracks = 0x1654AE6B;
constexpr std::uint64_t kTrackEntry = 0xAE;
constexpr std::uint64_t kTrackNumber = 0xD7;
constexpr std::uint64_t kTrackUid = 0x73C5;
constexpr std::uint64_t kTrackType = 0x83;
constexpr std::uint64_t kTrackName = 0x536E;
constexpr std::uint64_t kCodecId = 0x86;
constexpr std::uint64_t kTags = 0x1254C367;
constexpr std::uint64_t kTag = 0x7373;
constexpr std::uint64_t kTargets = 0x63C0;
constexpr std::uint64_t kTargetTypeValue = 0x68CA;
constexpr std::uint64_t kTargetType = 0x63CA;
constexpr std::uint64_t kTargetTrackUid = 0x63C5;
constexpr std::uint64_t kTargetEditionUid = 0x63C9;
constexpr std::uint64_t kTargetChapterUid = 0x63C4;
constexpr std::uint64_t kTargetAttachmentUid = 0x63C6;
constexpr std::uint64_t kSimpleTag = 0x67C8;
constexpr std::uint64_t kTagName = 0x45A3;
constexpr std::uint64_t kTagLanguage = 0x447A;
constexpr std::uint64_t kTagLanguageIetf = 0x447B;
constexpr std::uint64_t kTagDefault = 0x4484;
constexpr std::uint64_t kTagString = 0x4487;
constexpr std::uint64_t kTagBinary = 0x4485;

struct Vint {
  std::uint64_t value = 0;
  std::uint8_t length = 0;
  bool unknown = false;
};

struct Element {
  std::uint64_t id = 0;
  std::uint64_t start = 0;
  std::uint64_t data_start = 0;
  std::uint64_t data_size = 0;
  std::uint64_t end = 0;
  bool unknown_size = false;
};

std::uint64_t local_file_size(const std::filesystem::path& path) {
  return static_cast<std::uint64_t>(std::filesystem::file_size(path));
}

std::uint8_t vint_length_from_first_byte(std::uint8_t first) {
  for (std::uint8_t length = 1; length <= 8; ++length) {
    if ((first & (0x80 >> (length - 1))) != 0) {
      return length;
    }
  }
  throw std::runtime_error("Invalid EBML variable integer.");
}

Vint read_vint(std::ifstream& input, bool keep_marker) {
  const auto first_raw = input.get();
  if (first_raw == EOF) {
    throw std::runtime_error("Unexpected end of file while reading EBML integer.");
  }

  const auto first = static_cast<std::uint8_t>(first_raw);
  const auto length = vint_length_from_first_byte(first);
  std::uint64_t value = keep_marker ? first : (first & (0xFF >> length));
  bool all_ones = !keep_marker && value == static_cast<std::uint64_t>(0xFF >> length);

  for (std::uint8_t i = 1; i < length; ++i) {
    const auto raw = input.get();
    if (raw == EOF) {
      throw std::runtime_error("Unexpected end of file while reading EBML integer.");
    }
    const auto byte = static_cast<std::uint8_t>(raw);
    value = (value << 8) | byte;
    all_ones = all_ones && byte == 0xFF;
  }

  return {value, length, all_ones};
}

Element read_element(std::ifstream& input, std::uint64_t container_end) {
  const auto start = static_cast<std::uint64_t>(input.tellg());
  const auto id = read_vint(input, true);
  const auto size = read_vint(input, false);
  const auto data_start = static_cast<std::uint64_t>(input.tellg());
  const auto end = size.unknown ? container_end : data_start + size.value;
  if (end > container_end) {
    throw std::runtime_error("EBML element exceeds its containing element.");
  }
  return {id.value, start, data_start, size.value, end, size.unknown};
}

std::string read_string(std::ifstream& input, const Element& element) {
  if (element.data_size == 0) {
    return {};
  }
  input.seekg(static_cast<std::streamoff>(element.data_start), std::ios::beg);
  std::string value(static_cast<std::size_t>(element.data_size), '\0');
  input.read(value.data(), static_cast<std::streamsize>(value.size()));
  if (!input) {
    throw std::runtime_error("Failed to read Matroska string element.");
  }
  return value;
}

std::uint64_t read_uint(std::ifstream& input, const Element& element) {
  if (element.data_size > 8) {
    throw std::runtime_error("Matroska integer element is too large.");
  }
  input.seekg(static_cast<std::streamoff>(element.data_start), std::ios::beg);
  std::uint64_t value = 0;
  for (std::uint64_t i = 0; i < element.data_size; ++i) {
    const auto byte = input.get();
    if (byte == EOF) {
      throw std::runtime_error("Failed to read Matroska integer element.");
    }
    value = (value << 8) | static_cast<std::uint8_t>(byte);
  }
  return value;
}

std::vector<std::uint8_t> read_binary(std::ifstream& input, const Element& element) {
  std::vector<std::uint8_t> value(static_cast<std::size_t>(element.data_size));
  if (value.empty()) {
    return value;
  }
  input.seekg(static_cast<std::streamoff>(element.data_start), std::ios::beg);
  input.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(value.size()));
  if (!input) {
    throw std::runtime_error("Failed to read Matroska binary element.");
  }
  return value;
}

TrackInfo parse_track(std::ifstream& input, const Element& entry) {
  TrackInfo track;
  auto offset = entry.data_start;
  while (offset < entry.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, entry.end);
    switch (child.id) {
      case kTrackNumber:
        track.number = read_uint(input, child);
        break;
      case kTrackUid:
        track.uid = read_uint(input, child);
        break;
      case kTrackType:
        track.type = read_uint(input, child);
        break;
      case kTrackName:
        track.name = read_string(input, child);
        break;
      case kCodecId:
        track.codec_id = read_string(input, child);
        break;
      default:
        break;
    }
    offset = child.end;
  }
  return track;
}

void parse_tracks(std::ifstream& input, const Element& tracks, TagDocument& document) {
  document.tracks.clear();
  auto offset = tracks.data_start;
  while (offset < tracks.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, tracks.end);
    if (child.id == kTrackEntry) {
      document.tracks.push_back(parse_track(input, child));
    }
    offset = child.end;
  }

  std::sort(document.tracks.begin(), document.tracks.end(),
            [](const TrackInfo& left, const TrackInfo& right) {
              return left.number < right.number;
            });
}

TagTargets parse_targets(std::ifstream& input, const Element& targets_element) {
  TagTargets targets;
  auto offset = targets_element.data_start;
  while (offset < targets_element.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, targets_element.end);
    switch (child.id) {
      case kTargetTypeValue:
        targets.target_type_value = read_uint(input, child);
        break;
      case kTargetType:
        targets.target_type = read_string(input, child);
        break;
      case kTargetTrackUid:
        targets.track_uids.push_back(read_uint(input, child));
        break;
      case kTargetEditionUid:
        targets.edition_uids.push_back(read_uint(input, child));
        break;
      case kTargetChapterUid:
        targets.chapter_uids.push_back(read_uint(input, child));
        break;
      case kTargetAttachmentUid:
        targets.attachment_uids.push_back(read_uint(input, child));
        break;
      default:
        break;
    }
    offset = child.end;
  }
  return targets;
}

SimpleTag parse_simple_tag(std::ifstream& input, const Element& simple_element) {
  SimpleTag tag;
  auto offset = simple_element.data_start;
  while (offset < simple_element.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, simple_element.end);
    switch (child.id) {
      case kTagName:
        tag.name = read_string(input, child);
        break;
      case kTagLanguage:
        tag.language = read_string(input, child);
        break;
      case kTagLanguageIetf:
        tag.language_bcp47 = read_string(input, child);
        break;
      case kTagDefault:
        tag.is_default = read_uint(input, child) != 0;
        break;
      case kTagString:
        tag.value_type = TagValueType::String;
        tag.string_value = read_string(input, child);
        break;
      case kTagBinary:
        tag.value_type = TagValueType::Binary;
        tag.binary_value = read_binary(input, child);
        break;
      case kSimpleTag:
        tag.children.push_back(parse_simple_tag(input, child));
        break;
      default:
        break;
    }
    offset = child.end;
  }
  return tag;
}

TagEntry parse_tag(std::ifstream& input, const Element& tag_element) {
  TagEntry entry;
  auto offset = tag_element.data_start;
  while (offset < tag_element.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, tag_element.end);
    if (child.id == kTargets) {
      entry.targets = parse_targets(input, child);
    } else if (child.id == kSimpleTag) {
      entry.simple_tags.push_back(parse_simple_tag(input, child));
    }
    offset = child.end;
  }
  return entry;
}

void parse_tags(std::ifstream& input, const Element& tags, TagDocument& document) {
  document.tags.clear();
  auto offset = tags.data_start;
  while (offset < tags.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, tags.end);
    if (child.id == kTag) {
      document.tags.push_back(parse_tag(input, child));
    }
    offset = child.end;
  }
}

Element find_segment(std::ifstream& input, std::uint64_t end) {
  auto offset = std::uint64_t{0};
  while (offset < end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto element = read_element(input, end);
    if (element.id == kSegment) {
      return element;
    }
    if (element.unknown_size) {
      break;
    }
    offset = element.end;
  }
  throw std::runtime_error("No Matroska segment was found in the file.");
}

}  // namespace

TagDocument load_tag_document(const std::filesystem::path& path) {
  TagDocument document;
  document.path = path;

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open Matroska file.");
  }

  const auto segment = find_segment(input, local_file_size(path));
  auto offset = segment.data_start;
  while (offset < segment.end) {
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    const auto child = read_element(input, segment.end);
    if (child.id == kTracks) {
      parse_tracks(input, child, document);
    } else if (child.id == kTags) {
      parse_tags(input, child, document);
    }
    offset = child.end;
  }

  return document;
}

}  // namespace mte
