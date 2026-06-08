#include "core/matroska_writer.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ebml/EbmlBinary.h"
#include "ebml/EbmlElement.h"
#include "ebml/EbmlMaster.h"
#include "ebml/EbmlStream.h"
#include "ebml/IOCallback.h"
#include "ebml/StdIOCallback.h"
#include "matroska/KaxCluster.h"
#include "matroska/KaxContexts.h"
#include "matroska/KaxSegment.h"
#include "matroska/KaxTags.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mte {
namespace {

using libebml::EbmlElement;
using libebml::EbmlMaster;
using libebml::EbmlStream;
using libebml::IOCallback;
using libebml::StdIOCallback;
using libmatroska::Context_KaxMatroska;
using libmatroska::Context_KaxSegment;
using libmatroska::KaxCluster;
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

constexpr std::uint64_t kMatroskaSegmentId = 0x18538067;
constexpr auto kMaxScanSize = std::numeric_limits<uint64>::max();
constexpr std::size_t kCopyBufferSize = 1024 * 1024;

struct Vint {
  std::uint64_t value = 0;
  std::uint8_t length = 0;
  bool unknown = false;
};

struct SegmentHeader {
  std::uint64_t element_start = 0;
  std::uint64_t data_start = 0;
  std::uint64_t size_offset = 0;
  std::uint8_t size_length = 0;
  std::uint64_t size_value = 0;
  bool size_unknown = false;
};

struct SegmentLayout {
  SegmentHeader header;
  std::uint64_t insert_offset = 0;
  std::uint64_t tags_start = 0;
  std::uint64_t tags_end = 0;
  bool has_tags = false;
};

template <typename T>
T& add_child(EbmlMaster& parent) {
  auto* child = parent.AddNewElt(EBML_INFO(T));
  if (!child) {
    throw std::runtime_error("Failed to create Matroska element.");
  }
  return *dynamic_cast<T*>(child);
}

template <typename T>
T& ensure_child(EbmlMaster& parent) {
  auto* child = parent.FindFirstElt(EBML_INFO(T), true);
  if (!child) {
    throw std::runtime_error("Failed to create Matroska element.");
  }
  return *dynamic_cast<T*>(child);
}

void add_uint_child(EbmlMaster& parent,
                    const libebml::EbmlCallbacks& callbacks,
                    std::uint64_t value) {
  auto* child = parent.AddNewElt(callbacks);
  if (!child) {
    throw std::runtime_error("Failed to create Matroska integer element.");
  }
  *dynamic_cast<libebml::EbmlUInteger*>(child) = value;
}

void add_simple_tag(EbmlMaster& parent, const SimpleTag& source) {
  auto& simple = add_child<KaxTagSimple>(parent);
  ensure_child<KaxTagName>(simple).SetValueUTF8(source.name);

  if (!source.language.empty()) {
    add_child<KaxTagLangue>(simple).SetValue(source.language);
  }
  if (!source.language_bcp47.empty()) {
    add_child<KaxTagLanguageIETF>(simple).SetValue(source.language_bcp47);
  }

  if (source.value_type == TagValueType::Binary) {
    auto& binary_tag = add_child<KaxTagBinary>(simple);
    if (!source.binary_value.empty()) {
      binary_tag.CopyBuffer(reinterpret_cast<const ::binary*>(source.binary_value.data()),
                            static_cast<std::uint32_t>(source.binary_value.size()));
    }
  } else {
    ensure_child<KaxTagString>(simple).SetValueUTF8(source.string_value);
  }
  if (!source.is_default) {
    add_child<KaxTagDefault>(simple).SetValue(0);
  }

  for (const auto& child : source.children) {
    add_simple_tag(simple, child);
  }
}

void populate_tags_element(KaxTags& tags, const TagDocument& document) {
  for (const auto& source : document.tags) {
    auto& tag = add_child<KaxTag>(tags);
    auto& targets = ensure_child<KaxTagTargets>(tag);
    if (source.targets.target_type_value != 50) {
      add_child<KaxTagTargetTypeValue>(targets).SetValue(source.targets.target_type_value);
    }
    if (!source.targets.target_type.empty()) {
      add_child<KaxTagTargetType>(targets).SetValue(source.targets.target_type);
    }

    for (const auto uid : source.targets.track_uids) {
      add_uint_child(targets, EBML_INFO(KaxTagTrackUID), uid);
    }
    for (const auto uid : source.targets.edition_uids) {
      add_uint_child(targets, EBML_INFO(KaxTagEditionUID), uid);
    }
    for (const auto uid : source.targets.chapter_uids) {
      add_uint_child(targets, EBML_INFO(KaxTagChapterUID), uid);
    }
    for (const auto uid : source.targets.attachment_uids) {
      add_uint_child(targets, EBML_INFO(KaxTagAttachmentUID), uid);
    }

    for (const auto& simple : source.simple_tags) {
      add_simple_tag(tag, simple);
    }
  }
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open temporary Matroska tags file.");
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> render_tags(const TagDocument& document) {
  const auto temp_path =
      std::filesystem::temp_directory_path() /
      ("matroska-tags-editor-tags-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".ebml");
  {
    KaxTags tags;
    populate_tags_element(tags, document);
    StdIOCallback output(temp_path.string().c_str(), MODE_CREATE);
    tags.Render(output, false);
  }
  auto bytes = read_file_bytes(temp_path);
  std::filesystem::remove(temp_path);
  return bytes;
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

std::vector<std::uint8_t> encode_size_vint(std::uint64_t value, std::uint8_t length) {
  if (length == 0 || length > 8) {
    throw std::runtime_error("Invalid EBML size length.");
  }

  const auto max_value = (length == 8) ? ((std::uint64_t{1} << 56) - 2)
                                      : ((std::uint64_t{1} << (7 * length)) - 2);
  if (value > max_value) {
    throw std::runtime_error("The updated Segment size does not fit the original EBML size field.");
  }

  std::vector<std::uint8_t> bytes(length);
  for (std::uint8_t i = 0; i < length; ++i) {
    const auto shift = 8 * (length - i - 1);
    bytes[i] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
  }
  bytes[0] |= static_cast<std::uint8_t>(0x80 >> (length - 1));
  return bytes;
}

SegmentHeader find_segment_header(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Failed to open Matroska file.");
  }

  while (input) {
    const auto element_start = static_cast<std::uint64_t>(input.tellg());
    const auto id = read_vint(input, true);
    const auto size_offset = static_cast<std::uint64_t>(input.tellg());
    const auto size = read_vint(input, false);
    const auto data_start = static_cast<std::uint64_t>(input.tellg());

    if (id.value == kMatroskaSegmentId) {
      return {element_start, data_start, size_offset, size.length, size.value, size.unknown};
    }

    if (size.unknown) {
      throw std::runtime_error("Unexpected unknown-sized EBML element before Segment.");
    }
    input.seekg(static_cast<std::streamoff>(size.value), std::ios::cur);
  }

  throw std::runtime_error("No Matroska segment was found in the file.");
}

void skip_element(IOCallback& input, const EbmlElement& element) {
  if (element.IsFiniteSize()) {
    input.setFilePointer(element.GetEndPosition(), libebml::seek_beginning);
  }
}

SegmentLayout read_segment_layout(const std::filesystem::path& path) {
  SegmentLayout layout;
  layout.header = find_segment_header(path);
  layout.insert_offset = layout.header.size_unknown
                             ? std::numeric_limits<std::uint64_t>::max()
                             : layout.header.data_start + layout.header.size_value;

  StdIOCallback input(path.string().c_str(), MODE_READ);
  input.setFilePointer(layout.header.element_start, libebml::seek_beginning);
  EbmlStream stream(input);
  int upper_level = 0;
  std::unique_ptr<EbmlElement> segment_element(
      stream.FindNextElement(Context_KaxMatroska, upper_level, kMaxScanSize, true));
  auto* segment = dynamic_cast<KaxSegment*>(segment_element.get());
  if (!segment) {
    throw std::runtime_error("No Matroska segment was found in the file.");
  }

  const auto segment_end = segment->IsFiniteSize()
                               ? segment->GetEndPosition()
                               : std::numeric_limits<uint64>::max();
  input.setFilePointer(segment->GetDataStart(), libebml::seek_beginning);
  EbmlStream segment_stream(input);

  while (input.getFilePointer() < segment_end) {
    int child_upper = 0;
    std::unique_ptr<EbmlElement> child(segment_stream.FindNextElement(
        Context_KaxSegment, child_upper, kMaxScanSize, true));
    if (!child || child_upper > 0) {
      break;
    }

    if (dynamic_cast<KaxTags*>(child.get())) {
      layout.has_tags = true;
      layout.tags_start = child->GetElementPosition();
      layout.tags_end = child->GetEndPosition();
      layout.insert_offset = layout.tags_start;
      break;
    }

    if (dynamic_cast<KaxCluster*>(child.get()) &&
        layout.insert_offset == std::numeric_limits<std::uint64_t>::max()) {
      layout.insert_offset = child->GetElementPosition();
      break;
    }

    skip_element(input, *child);
  }

  if (layout.insert_offset == std::numeric_limits<std::uint64_t>::max()) {
    layout.insert_offset = layout.header.size_unknown
                               ? static_cast<std::uint64_t>(
                                     std::filesystem::file_size(path))
                               : layout.header.data_start + layout.header.size_value;
  }

  return layout;
}

std::vector<std::uint8_t> make_void(std::uint64_t size) {
  if (size == 0) {
    return {};
  }
  if (size == 1) {
    throw std::runtime_error("Cannot fill a one-byte EBML gap with Void.");
  }

  for (std::uint8_t size_length = 1; size_length <= 8; ++size_length) {
    const auto header_size = std::uint64_t{1} + size_length;
    if (size < header_size) {
      continue;
    }
    const auto payload_size = size - header_size;
    try {
      auto encoded_size = encode_size_vint(payload_size, size_length);
      std::vector<std::uint8_t> bytes;
      bytes.reserve(static_cast<std::size_t>(size));
      bytes.push_back(0xEC);
      bytes.insert(bytes.end(), encoded_size.begin(), encoded_size.end());
      bytes.resize(static_cast<std::size_t>(size), 0);
      return bytes;
    } catch (const std::runtime_error&) {
    }
  }

  throw std::runtime_error("Cannot create EBML Void filler for the requested size.");
}

void copy_range(std::ifstream& input,
                std::ofstream& output,
                std::uint64_t start,
                std::uint64_t end) {
  if (end < start) {
    throw std::runtime_error("Invalid file copy range.");
  }

  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);
  std::array<char, kCopyBufferSize> buffer{};
  auto remaining = end - start;
  while (remaining > 0) {
    const auto chunk =
        static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
    input.read(buffer.data(), chunk);
    if (input.gcount() != chunk) {
      throw std::runtime_error("Failed to read Matroska file while saving.");
    }
    output.write(buffer.data(), chunk);
    if (!output) {
      throw std::runtime_error("Failed to write temporary Matroska file.");
    }
    remaining -= static_cast<std::uint64_t>(chunk);
  }
}

void replace_file(const std::filesystem::path& source, const std::filesystem::path& target) {
#ifdef _WIN32
  if (!MoveFileExW(source.wstring().c_str(), target.wstring().c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("Failed to replace original Matroska file.");
  }
#else
  std::filesystem::rename(source, target);
#endif
}

std::filesystem::path temp_save_path(const std::filesystem::path& path) {
  return path.parent_path() /
         (path.filename().string() + ".matroska-tags-editor.tmp");
}

void write_modified_file(const TagDocument& document,
                         const SegmentLayout& layout,
                         const std::vector<std::uint8_t>& tags_bytes) {
  const auto source_size = std::filesystem::file_size(document.path);
  const auto temp_path = temp_save_path(document.path);

  std::ifstream input(document.path, std::ios::binary);
  std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
  if (!input || !output) {
    throw std::runtime_error("Failed to open files for Matroska save.");
  }

  std::uint64_t replaced_start = layout.insert_offset;
  std::uint64_t replaced_end = layout.insert_offset;
  if (layout.has_tags) {
    replaced_start = layout.tags_start;
    replaced_end = layout.tags_end;
  }

  const auto old_size = replaced_end - replaced_start;
  const auto new_size = static_cast<std::uint64_t>(tags_bytes.size());
  const auto grows = new_size > old_size;
  const auto delta = grows ? new_size - old_size : 0;

  std::vector<std::uint8_t> updated_segment_size;
  if (grows && !layout.header.size_unknown) {
    updated_segment_size =
        encode_size_vint(layout.header.size_value + delta, layout.header.size_length);
  }

  if (updated_segment_size.empty()) {
    copy_range(input, output, 0, replaced_start);
  } else {
    copy_range(input, output, 0, layout.header.size_offset);
    output.write(reinterpret_cast<const char*>(updated_segment_size.data()),
                 static_cast<std::streamsize>(updated_segment_size.size()));
    copy_range(input, output, layout.header.size_offset + layout.header.size_length,
               replaced_start);
  }

  output.write(reinterpret_cast<const char*>(tags_bytes.data()),
               static_cast<std::streamsize>(tags_bytes.size()));

  if (old_size > new_size) {
    const auto filler = make_void(old_size - new_size);
    output.write(reinterpret_cast<const char*>(filler.data()),
                 static_cast<std::streamsize>(filler.size()));
  }

  copy_range(input, output, replaced_end, source_size);
  output.close();
  input.close();

  replace_file(temp_path, document.path);
}

}  // namespace

void save_tag_document(const TagDocument& document) {
  if (document.path.empty()) {
    throw std::runtime_error("Cannot save a document without a file path.");
  }

  const auto tags_bytes = render_tags(document);
  const auto layout = read_segment_layout(document.path);
  write_modified_file(document, layout, tags_bytes);
}

}  // namespace mte
