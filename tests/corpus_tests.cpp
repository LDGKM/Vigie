#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "corpus/chunker.hpp"
#include "corpus/extract_html.hpp"
#include "corpus/index_store.hpp"

using namespace vigie::corpus;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "ECHEC : " << name << '\n';
        ++failures;
    }
}

bool is_valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t size = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (size == 0 || i + size > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < size; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) >> 6) != 0x2) {
                return false;
            }
        }
        i += size;
    }
    return true;
}

void test_clean_text() {
    check(clean_text("  L&rsquo;&eacute;t&eacute;&nbsp;&amp;\n  co ") == "L’été & co", "entites et espaces");
    check(clean_text("&#233;&#xE9;") == "éé", "entites numeriques");
    check(clean_text("a & b &inconnu;") == "a & b &inconnu;", "entite inconnue conservee");
}

void test_eu_law_sections() {
    const auto paragraphs = extract_html(R"html(
        <p class="oj-doc-ti">RÈGLEMENT (UE) 2023/1114</p>
        <p class="oj-normal">Considérant.</p>
        <p class="oj-ti-section-1">TITRE I</p>
        <div id="X.art_1"><p class="oj-ti-art">Article premier</p>
        <p class="oj-sti-art">Objet</p>
        <p class="oj-normal">1. Texte.</p>
        <p class="oj-note">(1) Note.</p>
        <div id="art_2"><p class="oj-ti-art">Article 2</p></div>
        <table><tr><td>a)</td><td>liste</td></tr></table>
        <p class="oj-normal">Fait à Bruxelles, le 31 mai 2023.</p>
        <p class="oj-doc-ti">ANNEXE I</p>
        <p class="oj-ti-grseq-1">Informations</p>
    )html");

    check(paragraphs.size() == 4, "nombre de paragraphes");
    if (paragraphs.size() == 4) {
        check(paragraphs[0].section == "Préambule", "preambule");
        check(paragraphs[1].section == "Article 1 : Objet", "article et titre");
        check(paragraphs[2].section == "Article 2" && paragraphs[2].text == "a) liste", "tableau");
        check(paragraphs[3].section == "ANNEXE I", "annexe");
    }
}

void test_quoted_articles_stay_in_their_article() {
    const auto paragraphs = extract_html(R"html(
        <div id="art_1"><p class="oj-ti-art">Article 1</p>
        <p class="oj-sti-art">Modification</p>
        <p class="oj-normal">L'article 4 est remplacé par :</p>
        <p class="oj-ti-art">«Article 4</p>
        <p class="oj-sti-art">Maîtrise de l'IA</p>
        <p class="oj-normal">Nouveau texte.</p>
        <p class="oj-ti-art">Article 4 bis</p>
        <p class="oj-normal">Suite.»</p>
        <div id="art_2"><p class="oj-ti-art">Article 2</p>
        <p class="oj-normal">Le «terme» est défini.</p></div>
    )html");

    check(paragraphs.size() == 7, "article cite conserve");
    if (paragraphs.size() == 7) {
        for (std::size_t i = 0; i < 6; ++i) {
            check(paragraphs[i].section == "Article 1 : Modification", "section de l'article modificateur");
        }
        check(paragraphs[6].section == "Article 2", "fin de la citation");
    }
}

void test_web_page() {
    const auto paragraphs = extract_html(R"html(
        <nav><p>Menu</p></nav>
        <main><script>var a = "<p>non</p>";</script>
        <h2>Agrégateurs</h2><p>Texte utile.</p><!-- <p>non</p> -->
        <h2>Lire également</h2><p>Autre page.</p></main>
        <footer><p>Pied</p></footer>
    )html");

    check(paragraphs.size() == 1, "page web filtree");
    if (paragraphs.size() == 1) {
        check(paragraphs[0].section == "Agrégateurs" && paragraphs[0].text == "Texte utile.", "section de page");
    }

    const auto intro = extract_html("<main><p>Présentation.</p></main>");
    check(intro.size() == 1 && intro[0].section == "Introduction", "introduction de page");
}

void test_chunks() {
    std::string long_text;
    for (int i = 0; i < 200; ++i) {
        long_text += "Une phrase accentuée numéro " + std::to_string(i) + " pour l’épargne. ";
    }

    const auto chunks = make_chunks({{"Article 1", std::nullopt, long_text}, {"Article 2", std::nullopt, "Court."}});
    check(chunks.size() > 3, "texte long decoupe");
    check(chunks.back().section == "Article 2" && chunks.back().text == "Court.", "pas de chevauchement entre sections");

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        check(is_valid_utf8(chunks[i].text), "UTF-8 valide");
        check(chunks[i].position == static_cast<int>(i), "positions continues");
        check(chunks[i].text.size() <= 1600, "taille maximale");
    }
    check(chunks[1].text.find("Une phrase") != std::string::npos, "contenu conserve");

    const auto unbroken = make_chunks({{"A", std::nullopt, std::string(3000, 'x')}});
    check(unbroken.size() == 1 && unbroken[0].text.size() == 3000, "mot sans espace conserve");
    check(make_chunks({}).empty(), "aucun paragraphe");

    std::vector<Paragraph> short_rows;
    for (int i = 0; i < 500; ++i) {
        short_rows.push_back({"Annexe", std::nullopt, "Ligne courte numéro " + std::to_string(i)});
    }
    for (const Chunk& chunk : make_chunks(short_rows)) {
        check(chunk.text.size() <= 1700, "lignes courtes limitees");
    }
}

void test_failed_index_keeps_previous_database() {
    const auto directory = std::filesystem::temp_directory_path() / "vigie_tests";
    std::filesystem::remove_all(directory);

    DocumentRecord record;
    record.id = "doc";
    const std::vector<IndexedDocument> valid = {{record, {{0, "S", std::nullopt, "texte"}}}};
    write_index(directory / "vigie.db", valid);

    const auto size = std::filesystem::file_size(directory / "vigie.db");
    bool failed = false;
    try {
        write_index(directory / "vigie.db", {{record, {}}, {record, {}}});
    } catch (const std::exception&) {
        failed = true;
    }

    check(failed, "doublon refuse");
    check(std::filesystem::file_size(directory / "vigie.db") == size, "base precedente conservee");
    check(!std::filesystem::exists(directory / "vigie.db.tmp"), "fichier temporaire supprime");
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    test_clean_text();
    test_eu_law_sections();
    test_quoted_articles_stay_in_their_article();
    test_web_page();
    test_chunks();
    test_failed_index_keeps_previous_database();

    if (failures == 0) {
        std::cout << "Tous les tests sont passes.\n";
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
