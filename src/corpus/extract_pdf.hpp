#pragma once

#include <filesystem>
#include <vector>

#include "corpus/types.hpp"

namespace vigie::corpus {

// Extrait le texte d'un PDF, une entrée par page. Lève std::runtime_error si le fichier est illisible.
std::vector<Paragraph> extract_pdf(const std::filesystem::path& path);

}  // namespace vigie::corpus
