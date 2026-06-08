#pragma once

#include <filesystem>

#include "core/tag_document.h"

namespace mte {

TagDocument load_tag_document(const std::filesystem::path& path);

}  // namespace mte
