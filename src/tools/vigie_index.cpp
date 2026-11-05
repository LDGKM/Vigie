#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "corpus/chunker.hpp"
#include "corpus/embedder.hpp"
#include "corpus/extract_html.hpp"
#include "corpus/extract_pdf.hpp"
#include "corpus/index_store.hpp"
#include "corpus/manifest.hpp"

namespace fs = std::filesystem;
using namespace vigie::corpus;

namespace {

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("fichier introuvable : " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<Paragraph> extract(const DocumentRecord& record, const fs::path& path) {
    if (record.format == "pdf") {
        return extract_pdf(path);
    }
    return extract_html(read_file(path));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path(VIGIE_DEFAULT_ROOT);
        const fs::path corpus_dir = root / "data" / "corpus";
        const fs::path database = root / "data" / "index" / "vigie.db";

        const std::string model_name = "multilingual-e5-small-f16.gguf";
        const fs::path model_path = root / "models" / model_name;
        if (!fs::exists(model_path)) {
            throw std::runtime_error("modele introuvable : lance scripts\\download_model.ps1");
        }
        const Embedder embedder(model_path);

        std::vector<IndexedDocument> documents;
        std::size_t total_chunks = 0;
        int failures = 0;

        for (const DocumentRecord& record : load_manifest(corpus_dir / "sources.json")) {
            const fs::path path = corpus_dir / "raw" / (record.id + "." + record.format);
            try {
                std::vector<Chunk> chunks = make_chunks(extract(record, path));
                if (chunks.empty()) {
                    throw std::runtime_error("aucun texte extrait");
                }

                std::vector<std::vector<float>> vectors;
                vectors.reserve(chunks.size());
                for (const Chunk& chunk : chunks) {
                    vectors.push_back(embedder.embed_passage(chunk.section + " : " + chunk.text));
                    if (vectors.size() % 50 == 0) {
                        std::cout << "        " << record.id << " : " << vectors.size() << '/' << chunks.size()
                                  << std::endl;
                    }
                }

                std::cout << "[OK]    " << record.id << " : " << chunks.size() << " passages" << std::endl;
                total_chunks += chunks.size();
                documents.push_back({record, std::move(chunks), std::move(vectors)});
            } catch (const std::exception& error) {
                std::cout << "[ECHEC] " << record.id << " : " << error.what() << '\n';
                ++failures;
            }
        }

        if (failures > 0) {
            std::cerr << "Index non modifie : corrige les " << failures << " echec(s), puis relance.\n";
            return 1;
        }

        write_index(database, documents, model_name);
        std::cout << documents.size() << " documents, " << total_chunks << " passages : "
                  << database.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << '\n';
        return 1;
    }

    return 0;
}
