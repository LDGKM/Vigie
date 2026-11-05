#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "corpus/types.hpp"

namespace vigie::corpus {

struct IndexedDocument {
    DocumentRecord record;
    std::vector<Chunk> chunks;
    std::vector<std::vector<float>> vectors;  // Un vecteur par passage.
};

// Écrit une nouvelle base puis remplace l'ancienne seulement si tout a réussi.
void write_index(const std::filesystem::path& database_path, const std::vector<IndexedDocument>& documents,
                 const std::string& embedding_model);

}  // namespace vigie::corpus
