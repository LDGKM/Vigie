#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "corpus/types.hpp"

namespace vigie::corpus {

// Extrait le texte utile d'une page et le rattache à son article, annexe ou titre.
std::vector<Paragraph> extract_html(std::string_view html);

// Décode les entités HTML et réduit les espaces.
std::string clean_text(std::string_view raw);

}  // namespace vigie::corpus
