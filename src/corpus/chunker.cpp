#include "corpus/chunker.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace vigie::corpus {
namespace {

// Découpe après une fin de phrase, ou à défaut après un espace, jamais au milieu d'un mot.
std::vector<std::string> split_long_text(const std::string& text, std::size_t limit) {
    std::vector<std::string> parts;
    std::string_view rest(text);

    while (rest.size() > limit) {
        std::size_t cut = std::string_view::npos;
        for (const std::string_view mark : {". ", " ; ", " : "}) {
            const std::size_t found = rest.rfind(mark, limit);
            if (found != std::string_view::npos && found > limit / 2) {
                cut = found + mark.size();
                break;
            }
        }
        if (cut == std::string_view::npos) {
            const std::size_t space = rest.rfind(' ', limit);
            cut = space == std::string_view::npos || space == 0 ? rest.find(' ', limit) : space + 1;
        }
        if (cut == std::string_view::npos) {
            break;
        }

        std::string part(rest.substr(0, cut));
        while (!part.empty() && part.back() == ' ') {
            part.pop_back();
        }
        parts.push_back(std::move(part));
        rest.remove_prefix(cut);
    }

    if (!rest.empty()) {
        parts.emplace_back(rest);
    }
    return parts;
}

std::string overlap_tail(const std::string& text, std::size_t size) {
    if (text.size() <= size) {
        return {};
    }
    const std::size_t space = text.find(' ', text.size() - size);
    return space == std::string::npos ? std::string{} : text.substr(space + 1);
}

}  // namespace

std::vector<Chunk> make_chunks(const std::vector<Paragraph>& paragraphs, const ChunkOptions& options) {
    std::vector<Chunk> chunks;
    std::string buffer;
    std::string section;
    std::optional<int> page;
    std::string overlap;
    bool has_new_text = false;

    const auto push_chunk = [&] {
        if (!has_new_text) {
            return;
        }
        chunks.push_back({static_cast<int>(chunks.size()), section, page, buffer});
        overlap = overlap_tail(buffer, options.overlap_bytes);
        buffer.clear();
        has_new_text = false;
    };

    const auto append = [&](const std::string& text) {
        if (buffer.empty() && !overlap.empty()) {
            buffer = overlap + " ";
        } else if (!buffer.empty()) {
            buffer += ' ';
        }
        buffer += text;
        has_new_text = true;
    };

    for (const Paragraph& paragraph : paragraphs) {
        if (paragraph.section != section || paragraph.page != page) {
            push_chunk();
            section = paragraph.section;
            page = paragraph.page;
            overlap.clear();
        }

        for (const std::string& part : split_long_text(paragraph.text, options.target_bytes)) {
            if (has_new_text && buffer.size() + part.size() + 1 > options.target_bytes) {
                const bool fits = buffer.size() + part.size() + 1 <= options.maximum_bytes;
                if (part.size() < options.minimum_tail_bytes && fits) {
                    buffer += ' ';
                    buffer += part;
                    continue;
                }
                push_chunk();
            }
            append(part);
        }
    }

    push_chunk();
    return chunks;
}

}  // namespace vigie::corpus
