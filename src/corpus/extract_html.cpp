#include "corpus/extract_html.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace vigie::corpus {
namespace {

constexpr std::array<std::string_view, 8> kSkippedElements = {
    "script", "style", "noscript", "svg", "nav", "header", "footer", "form",
};

constexpr std::array<std::string_view, 19> kBlockElements = {
    "p",  "div", "li", "ul", "ol", "table", "tr", "h1", "h2", "h3",
    "h4", "h5",  "h6", "section", "article", "aside", "br", "blockquote", "hr",
};

constexpr std::array<std::pair<std::string_view, std::string_view>, 24> kEntities = {{
    {"amp", "&"},      {"lt", "<"},       {"gt", ">"},       {"quot", "\""},
    {"apos", "'"},     {"nbsp", " "},     {"rsquo", "’"},    {"lsquo", "‘"},
    {"laquo", "«"},    {"raquo", "»"},    {"ndash", "–"},    {"mdash", "—"},
    {"hellip", "…"},   {"euro", "€"},     {"eacute", "é"},   {"egrave", "è"},
    {"ecirc", "ê"},    {"agrave", "à"},   {"acirc", "â"},    {"ccedil", "ç"},
    {"ocirc", "ô"},    {"ucirc", "û"},    {"Eacute", "É"},   {"oelig", "œ"},
}};

bool is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out += text[i];
            continue;
        }

        const std::size_t end = text.find(';', i);
        if (end == std::string_view::npos || end - i > 10 || end == i + 1) {
            out += '&';
            continue;
        }

        const std::string_view name = text.substr(i + 1, end - i - 1);
        bool decoded = false;
        if (name.front() == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            const std::string digits(name.substr(hex ? 2 : 1));
            char* parsed = nullptr;
            const unsigned long code = std::strtoul(digits.c_str(), &parsed, hex ? 16 : 10);
            if (!digits.empty() && *parsed == '\0') {
                append_utf8(out, static_cast<std::uint32_t>(code));
                decoded = true;
            }
        } else {
            for (const auto& [entity, value] : kEntities) {
                if (entity == name) {
                    out += value;
                    decoded = true;
                    break;
                }
            }
        }

        if (decoded) {
            i = end;
        } else {
            out += '&';
        }
    }

    return out;
}

struct Tag {
    std::string name;
    std::string classes;
    std::string id;
    bool closing = false;
    bool self_closing = false;
};

std::string read_attribute(std::string_view tag, std::string_view attribute) {
    const std::string lowered = lower(tag);
    std::size_t position = 0;
    while ((position = lowered.find(attribute, position)) != std::string::npos) {
        std::size_t cursor = position + attribute.size();
        const bool starts_name = position > 0 && is_space(static_cast<unsigned char>(lowered[position - 1]));
        while (cursor < tag.size() && is_space(static_cast<unsigned char>(tag[cursor]))) {
            ++cursor;
        }
        if (starts_name && cursor < tag.size() && tag[cursor] == '=') {
            ++cursor;
            while (cursor < tag.size() && is_space(static_cast<unsigned char>(tag[cursor]))) {
                ++cursor;
            }
            if (cursor < tag.size() && (tag[cursor] == '"' || tag[cursor] == '\'')) {
                const std::size_t end = tag.find(tag[cursor], cursor + 1);
                if (end != std::string_view::npos) {
                    return std::string(tag.substr(cursor + 1, end - cursor - 1));
                }
            }
        }
        position += attribute.size();
    }
    return {};
}

Tag parse_tag(std::string_view inner) {
    Tag tag;
    std::size_t cursor = 0;
    if (!inner.empty() && inner.front() == '/') {
        tag.closing = true;
        cursor = 1;
    }

    std::size_t end = cursor;
    while (end < inner.size() && !is_space(static_cast<unsigned char>(inner[end])) && inner[end] != '/') {
        ++end;
    }

    tag.name = lower(inner.substr(cursor, end - cursor));
    tag.self_closing = !inner.empty() && inner.back() == '/';
    tag.classes = read_attribute(inner, "class");
    tag.id = read_attribute(inner, "id");
    return tag;
}

bool has_class(const std::string& classes, std::string_view name) {
    std::size_t position = 0;
    while ((position = classes.find(name, position)) != std::string::npos) {
        const std::size_t end = position + name.size();
        const bool starts = position == 0 || is_space(static_cast<unsigned char>(classes[position - 1]));
        const bool ends = end == classes.size() || is_space(static_cast<unsigned char>(classes[end]));
        if (starts && ends) {
            return true;
        }
        position = end;
    }
    return false;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// Reconnaît un identifiant EUR-Lex d'article : "art_12" ou "L_2023150FR.01004001.art_12".
bool is_article_id(std::string_view id) {
    const std::size_t dot = id.rfind('.');
    const std::string_view last = dot == std::string_view::npos ? id : id.substr(dot + 1);
    if (!starts_with(last, "art_") || last.size() == 4) {
        return false;
    }
    return std::all_of(last.begin() + 4, last.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Trouve la fin d'une balise en ignorant les ">" placés dans un attribut.
std::size_t find_tag_end(std::string_view html, std::size_t start) {
    char quote = '\0';
    for (std::size_t i = start; i < html.size(); ++i) {
        const char c = html[i];
        if (quote != '\0') {
            if (c == quote) {
                quote = '\0';
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return std::string_view::npos;
}

std::string_view main_content(std::string_view html) {
    const std::string lowered = lower(html);
    const std::size_t open = lowered.find("<main");
    if (open == std::string::npos) {
        return html;
    }
    const std::size_t close = lowered.rfind("</main>");
    if (close == std::string::npos || close < open) {
        return html.substr(open);
    }
    return html.substr(open, close - open);
}

// Ces titres ouvrent la fin de page : liens, téléchargements et formulaires, sans contenu utile.
bool ends_content(std::string_view heading) {
    constexpr std::array<std::string_view, 4> kClosingHeadings = {
        "Lire également",
        "Télécharger l'intégralité de la publication",
        "Qui peut m'aider ?",
        "Textes de loi et références",
    };
    return std::find(kClosingHeadings.begin(), kClosingHeadings.end(), heading) != kClosingHeadings.end();
}

// Boutons et messages de partage ou d'abonnement, présents sur chaque fiche Service Public.
bool is_page_chrome(std::string_view text) {
    constexpr std::array<std::string_view, 3> kWholeTexts = {"Focus", "Tout déplier", "Image image"};
    constexpr std::array<std::string_view, 16> kPrefixes = {
        "Ajouter à mes favoris",     "Partager la page",     "Ce sujet vous intéresse",
        "Ce sujet a été ajouté",     "Vous recevrez un courriel", "Votre abonnement",
        "Vous devez vous connecter", "Pour vous abonner",    "Vous serez alerté",
        "Fermer",                    "Facebook",             "Copier le lien",
        "Lien copié",                "Ajoutez cette page",   "Vous pouvez à tout moment",
        "X Linkedin",
    };
    return std::find(kWholeTexts.begin(), kWholeTexts.end(), text) != kWholeTexts.end() ||
           std::any_of(kPrefixes.begin(), kPrefixes.end(),
                       [text](std::string_view prefix) { return starts_with(text, prefix); });
}

class SectionBuilder {
public:
    std::vector<Paragraph> take() { return std::move(paragraphs_); }

    bool finished() const { return finished_; }

    void set_block_class(const std::string& classes) {
        for (const std::string_view name : kKnownClasses) {
            if (has_class(classes, name)) {
                block_class_ = std::string(name);
                return;
            }
        }
    }

    void set_heading(int level) { heading_level_ = level; }

    void open_article_container() { article_container_open_ = true; }

    void add_text(std::string_view text) { buffer_.append(text); }

    void flush() {
        const std::string text = clean_text(buffer_);
        buffer_.clear();
        const std::string block_class = std::exchange(block_class_, {});
        if (text.empty()) {
            return;  // Un titre peut commencer par des blocs vides : il reste en attente.
        }
        handle(text, block_class, std::exchange(heading_level_, 0));
    }

private:
    static constexpr std::array<std::string_view, 8> kKnownClasses = {
        "oj-ti-art", "oj-sti-art", "oj-doc-ti", "oj-ti-section-1",
        "oj-ti-section-2", "oj-note", "oj-signatory", "oj-final",
    };

    void handle(std::string text, const std::string& block_class, int heading) {
        // La date est déjà dans la fiche du document : ici, elle cacherait le début du texte.
        constexpr std::string_view kSignature = "(Premier ministre)";
        if (starts_with(text, "Vérifié le ")) {
            const std::size_t end = text.find(kSignature);
            if (end != std::string::npos) {
                text.erase(0, end + kSignature.size());
                text.erase(0, std::min(text.find_first_not_of(' '), text.size()));
            }
        }
        // Reste des infobulles de Service Public, invisibles sur la page.
        for (const std::string_view marker : {std::string_view(": titleContent"), std::string_view("titleContent")}) {
            for (std::size_t at = text.find(marker); at != std::string::npos; at = text.find(marker, at)) {
                text.erase(at, marker.size());
            }
        }
        if (block_class == "oj-note" || block_class == "oj-signatory" || block_class == "oj-final") {
            return;
        }
        if (starts_with(block_class, "oj-hd") || starts_with(text, "Fait à ") ||
            is_page_chrome(text)) {
            return;
        }
        // Seuls les vrais articles ont un conteneur art_N. Un titre sans conteneur est un article
        // cité par un texte modificateur : il reste dans l'article qui le modifie.
        const bool real_article = std::exchange(article_container_open_, false) && block_class == "oj-ti-art";

        if (real_article) {
            section_ = text == "Article premier" ? "Article 1" : text;
            in_annex_ = false;
            expects_subtitle_ = true;
            official_journal_ = true;
            return;
        }
        if (block_class == "oj-sti-art" && expects_subtitle_) {
            section_ += " : " + text;
            expects_subtitle_ = false;
            return;
        }
        expects_subtitle_ = false;

        if (block_class == "oj-doc-ti") {
            official_journal_ = true;
            if (starts_with(text, "ANNEXE")) {
                section_ = text;
                in_annex_ = true;
            }
            return;
        }
        if ((block_class == "oj-ti-section-1" || block_class == "oj-ti-section-2") && !in_annex_ &&
            !starts_with(text, "«")) {
            return;
        }
        if (heading > 0) {
            if (ends_content(text)) {
                finished_ = true;
                return;
            }
            section_ = text;
            return;
        }

        const std::string fallback = official_journal_ ? "Préambule" : "Introduction";
        paragraphs_.push_back({section_.empty() ? fallback : section_, std::nullopt, text});
    }

    std::vector<Paragraph> paragraphs_;
    std::string buffer_;
    std::string section_;
    std::string block_class_;
    int heading_level_ = 0;
    bool article_container_open_ = false;
    bool in_annex_ = false;
    bool expects_subtitle_ = false;
    bool official_journal_ = false;
    bool finished_ = false;
};

}  // namespace

std::string clean_text(std::string_view raw) {
    const std::string decoded = decode_entities(raw);
    std::string out;
    out.reserve(decoded.size());
    bool pending_space = false;

    for (std::size_t i = 0; i < decoded.size();) {
        const auto c = static_cast<unsigned char>(decoded[i]);
        std::size_t width = 0;
        if (is_space(c)) {
            width = 1;
        } else if (c == 0xC2 && i + 1 < decoded.size() && static_cast<unsigned char>(decoded[i + 1]) == 0xA0) {
            width = 2;  // Espace insécable.
        } else if (c == 0xE2 && i + 2 < decoded.size() && static_cast<unsigned char>(decoded[i + 1]) == 0x80) {
            const auto third = static_cast<unsigned char>(decoded[i + 2]);
            if ((third >= 0x80 && third <= 0x8A) || third == 0xAF) {
                width = 3;  // Espaces typographiques.
            }
        }

        if (width > 0) {
            pending_space = true;
            i += width;
            continue;
        }
        if (pending_space && !out.empty()) {
            out += ' ';
        }
        pending_space = false;
        out += decoded[i++];
    }

    return out;
}

std::vector<Paragraph> extract_html(std::string_view html) {
    const std::string_view content = main_content(html);
    SectionBuilder builder;
    std::string skipped;
    int skipped_depth = 0;

    for (std::size_t i = 0; i < content.size() && !builder.finished();) {
        if (content[i] != '<') {
            const std::size_t next = std::min(content.find('<', i), content.size());
            if (skipped_depth == 0) {
                builder.add_text(content.substr(i, next - i));
            }
            i = next;
            continue;
        }

        if (content.compare(i, 4, "<!--") == 0) {
            const std::size_t end = content.find("-->", i + 4);
            i = end == std::string_view::npos ? content.size() : end + 3;
            continue;
        }

        const std::size_t end = find_tag_end(content, i + 1);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view inner = content.substr(i + 1, end - i - 1);
        i = end + 1;
        if (!inner.empty() && (inner.front() == '!' || inner.front() == '?')) {
            continue;
        }

        const Tag tag = parse_tag(inner);
        if (skipped_depth > 0) {
            if (tag.name == skipped) {
                if (tag.closing) {
                    --skipped_depth;
                } else if (!tag.self_closing) {
                    ++skipped_depth;
                }
            }
            continue;
        }

        if (contains(kSkippedElements, tag.name) && !tag.closing && !tag.self_closing) {
            builder.flush();
            skipped = tag.name;
            skipped_depth = 1;
            continue;
        }

        if (contains(kBlockElements, tag.name)) {
            builder.flush();
            if (tag.closing && tag.name.size() == 2 && tag.name[0] == 'h' && tag.name[1] >= '1' && tag.name[1] <= '4') {
                builder.set_heading(0);
            }
            if (!tag.closing) {
                if (tag.name == "div" && is_article_id(tag.id)) {
                    builder.open_article_container();
                }
                builder.set_block_class(tag.classes);
                if (tag.name.size() == 2 && tag.name[0] == 'h' && tag.name[1] >= '1' && tag.name[1] <= '4') {
                    builder.set_heading(tag.name[1] - '0');
                }
            }
        } else if (tag.name == "td" || tag.name == "th") {
            builder.add_text(" ");
        }
    }

    builder.flush();
    return builder.take();
}

}  // namespace vigie::corpus
