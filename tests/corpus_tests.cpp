#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "core/grounding.hpp"
#include "corpus/chunker.hpp"
#include "corpus/embedder.hpp"
#include "corpus/extract_html.hpp"
#include "corpus/index_store.hpp"
#include "corpus/search.hpp"

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
    const std::vector<IndexedDocument> valid = {{record, {{0, "S", std::nullopt, "texte"}}, {{1.0F, 0.0F}}}};
    write_index(directory / "vigie.db", valid, "test");

    const auto size = std::filesystem::file_size(directory / "vigie.db");
    bool failed = false;
    try {
        write_index(directory / "vigie.db", {{record, {}, {}}, {record, {}, {}}}, "test");
    } catch (const std::exception&) {
        failed = true;
    }

    check(failed, "doublon refuse");
    check(std::filesystem::file_size(directory / "vigie.db") == size, "base precedente conservee");
    check(!std::filesystem::exists(directory / "vigie.db.tmp"), "fichier temporaire supprime");
    std::filesystem::remove_all(directory);
}

void test_extract_terms() {
    const auto terms = extract_terms("Qu’est-ce qu'un agrégateur de comptes « sécurisés » ? \" OR *");
    const std::vector<std::string> expected = {"agregateur", "compte", "securise", "or"};
    check(terms == expected, "mots utiles sans accents ni pluriel");
    check(extract_terms("Qui est-ce ?").empty(), "question sans mot utile");
    check(extract_terms("DSP2 2027 IA") == std::vector<std::string>({"dsp2", "2027", "ia"}), "sigles et nombres");
}

void test_search() {
    const auto directory = std::filesystem::temp_directory_path() / "vigie_search_tests";
    std::filesystem::remove_all(directory);
    const SearchIndex missing(directory / "vigie.db");

    bool unavailable = false;
    try {
        missing.search("agrégateur de comptes");
    } catch (const IndexUnavailable&) {
        unavailable = true;
    }
    check(unavailable, "base manquante signalee");

    const auto make_record = [](const std::string& id, const std::string& title) {
        DocumentRecord record;
        record.id = id;
        record.title = title;
        record.issuer = "Test";
        record.theme = "t";
        record.language = "fr";
        record.document_type = "t";
        record.format = "html";
        record.url = "https://example.org";
        record.version = "2026-01-01";
        record.version_status = "initiale";
        record.reuse = "t";
        return record;
    };

    const std::vector<float> vector = {1.0F, 0.0F};
    write_index(directory / "vigie.db",
                {{make_record("mica", "Règlement sur les crypto-actifs (MiCA)"),
                  {{0, "Préambule", std::nullopt, "Les jetons de monnaie électronique sont encadrés."},
                   {1, "Article 3 : Définitions", std::nullopt, "Un jeton de monnaie électronique est un crypto-actif."},
                   {2, "Article 9", std::nullopt, "Le livre blanc est publié par l’émetteur."}},
                  {vector, vector, vector}},
                 {make_record("guide", "Guide des paiements"),
                  {{0, "Page 2", 2, "Les agrégateurs de comptes regroupent les informations."}},
                  {vector}}},
                "test");

    check(load_vector_cache(directory / "vigie.db", "test").size() == 4, "cache des vecteurs");
    check(load_vector_cache(directory / "vigie.db", "autre").empty(), "cache lie au modele");

    const SearchIndex index(directory / "vigie.db");

    const auto definition = index.search("Qu'est-ce qu'un jeton de monnaie électronique ?");
    check(!definition.empty() && definition[0].section == "Article 3 : Définitions", "definition en premier");

    const auto accent = index.search("agregateur de compte");
    check(accent.size() == 1 && accent[0].document_id == "guide" && accent[0].page == 2, "accents et pluriel");

    check(!index.search("livre blanc MiCA").empty(), "nom du texte dans le titre");
    const auto named = index.search("MiCA émetteur");
    check(!named.empty() && named[0].document_id == "mica", "recherche dans le texte nomme");
    check(index.search("recette de la tarte aux pommes").empty(), "hors corpus");
    check(index.search("Comment fonctionne le livre blanc diesel").empty(), "mot rare absent");
    check(index.search("\" OR * NEAR( ) -- ;").empty(), "syntaxe FTS neutralisee");
    check(index.search("le la les").empty(), "mots vides seulement");

    std::filesystem::remove_all(directory);
}

// Ce test utilise le vrai modèle : indique son chemin dans VIGIE_TEST_MODEL pour l'activer.
void test_embedder() {
    const char* model_path = std::getenv("VIGIE_TEST_MODEL");
    if (model_path == nullptr) {
        std::cout << "Test du modele ignore.\n";
        return;
    }

    const Embedder embedder{std::filesystem::path(model_path)};
    const auto query = embedder.embed_query("obligations de transparence des chatbots");
    const auto related = embedder.embed_passage("Les personnes sont informées qu'elles interagissent avec un système d'IA.");
    const auto unrelated = embedder.embed_passage("Faire cuire une tarte aux pommes pendant trente minutes.");

    const auto dot = [](const std::vector<float>& left, const std::vector<float>& right) {
        return std::inner_product(left.begin(), left.end(), right.begin(), 0.0F);
    };

    check(query.size() == embedder.dimensions(), "dimension du vecteur");
    check(std::abs(dot(query, query) - 1.0F) < 0.001F, "vecteur normalise");
    check(dot(query, related) > dot(query, unrelated), "sens proche mieux classe");
}

}  // namespace

void test_html_headings() {
    const std::string page =
        "<main><h1>Livret A</h1><p>Un livret reglemente.</p>"
        "<h2><button><span><div></div>Quel est le plafond ?</span></button></h2><p>Le plafond est fixe.</p>"
        "<h2></h2><p>Ce texte reste un paragraphe.</p>"
        "<h2>Qui peut m&#39;aider ?</h2><p>Formulaire.</p></main>";
    const auto paragraphs = vigie::corpus::extract_html(page);
    check(paragraphs.size() == 3, "fin de page ignoree");
    const auto signed_page = vigie::corpus::extract_html(
        "<main><p>Vérifié le 01 août 2026 - Service Public (Premier ministre) Le livret est simple.</p></main>");
    check(signed_page.size() == 1 && signed_page[0].text == "Le livret est simple.", "signature retiree");
    check(paragraphs.size() > 1 && paragraphs[1].section == "Quel est le plafond ?", "titre avec blocs vides");
    check(paragraphs.size() > 2 && paragraphs[2].text == "Ce texte reste un paragraphe.", "titre vide sans effet");
}

void test_answer_check() {
    vigie::corpus::SearchHit first;
    first.title = "Directive";
    first.section = "Article 71";
    first.text = "Au plus tard treize mois, dans la limite de 50 000 EUR.";
    vigie::corpus::SearchHit second;
    second.title = "Directive";
    second.section = "Article 73";
    second.text = "Avant la fin du premier jour ouvrable suivant.";
    const std::vector<vigie::corpus::SearchHit> passages{first, second};

    const auto valid = vigie::check_answer("Le plafond est de 50000 EUR [1]. Le remboursement est rapide [1, 2].", passages);
    check(valid.check == vigie::AnswerCheck::Valid, "reponse valide");
    check(valid.citations == std::vector<int>({1, 2}), "citations sans doublon");
    check(vigie::check_answer("L'article 71 s'applique [1].", passages).check == vigie::AnswerCheck::Valid,
          "nombre present dans le titre de section");
    check(vigie::check_answer("Le plafond est de 50\xC2\xA0" "000 EUR [1].", passages).check == vigie::AnswerCheck::Valid,
          "espace insecable dans un nombre");

    check(vigie::check_answer("Le delai est de treize mois [1]. Cela protege le client.", passages).text ==
              "Le delai est de treize mois [1].",
          "fin sans citation retiree");
    check(vigie::check_answer("Le delai est de treize mois [1]. Il passera a 18 mois.", passages).check ==
              vigie::AnswerCheck::Valid,
          "nombre retire avec la fin non citee");

    vigie::corpus::SearchHit fine;
    fine.title = "Reglement";
    fine.section = "Sanctions";
    fine.text = "Une amende de 35 000 000 EUR.";
    check(vigie::check_answer("Une amende de 35 millions d'euros [1].", {fine}).check == vigie::AnswerCheck::Valid,
          "montant ecrit en millions");

    using enum vigie::AnswerCheck;
    check(vigie::check_answer("INFORMATION NON TROUVÉE", passages).check == NotFound, "refus du modele");
    check(vigie::check_answer("Le delai est de treize mois.", passages).check == Unconfirmed, "citation absente");
    check(vigie::check_answer("Le delai est de treize mois [3].", passages).check == Unconfirmed, "citation inventee");
    check(vigie::check_answer("Le delai est de 18 mois [1].", passages).check == Unconfirmed, "nombre invente");
    check(vigie::check_answer("Le plafond est de 50 000 EUR [2].", passages).check == Unconfirmed,
          "nombre absent du passage cite");
    check(vigie::check_answer("Une amende de 36 millions d'euros [1].", {fine}).check == Unconfirmed,
          "autre montant en millions");
    check(vigie::check_answer("Le plafond [a] est connu.", passages).check == Unconfirmed, "crochet qui n'est pas une citation");

    const std::string prompt = vigie::grounded_user_prompt("Question ?", passages);
    check(prompt.find("[2] Directive — Article 73") != std::string::npos, "passages numerotes");
}

int main() {
    test_clean_text();
    test_eu_law_sections();
    test_quoted_articles_stay_in_their_article();
    test_web_page();
    test_chunks();
    test_failed_index_keeps_previous_database();
    test_extract_terms();
    test_search();
    test_embedder();
    test_answer_check();
    test_html_headings();

    if (failures == 0) {
        std::cout << "Tous les tests sont passes.\n";
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
