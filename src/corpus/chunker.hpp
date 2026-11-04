#pragma once

#include <cstddef>
#include <vector>

#include "corpus/types.hpp"

namespace vigie::corpus {

struct ChunkOptions {
    std::size_t target_bytes = 1200;
    std::size_t overlap_bytes = 150;
    std::size_t minimum_tail_bytes = 250;
    std::size_t maximum_bytes = 1500;
};

// Regroupe les paragraphes d'une même section en passages de taille proche de la cible.
std::vector<Chunk> make_chunks(const std::vector<Paragraph>& paragraphs, const ChunkOptions& options = {});

}  // namespace vigie::corpus
