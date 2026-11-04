#pragma once

#include <filesystem>
#include <vector>

#include "corpus/types.hpp"

namespace vigie::corpus {

// Lève std::runtime_error si le fichier est illisible ou si une fiche est invalide.
std::vector<DocumentRecord> load_manifest(const std::filesystem::path& path);

}  // namespace vigie::corpus
