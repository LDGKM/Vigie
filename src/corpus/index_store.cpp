#include "corpus/index_store.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

#include "corpus/sqlite.hpp"

namespace vigie::corpus {
namespace {

constexpr const char* kSchema = R"sql(
PRAGMA user_version = 3;

CREATE TABLE documents (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    issuer TEXT NOT NULL,
    theme TEXT NOT NULL,
    language TEXT NOT NULL,
    document_type TEXT NOT NULL,
    format TEXT NOT NULL,
    url TEXT NOT NULL,
    published TEXT,
    version TEXT,
    version_status TEXT NOT NULL,
    reuse TEXT NOT NULL
);

CREATE TABLE chunks (
    id INTEGER PRIMARY KEY,
    document_id TEXT NOT NULL REFERENCES documents(id),
    position INTEGER NOT NULL,
    title TEXT NOT NULL,
    section TEXT NOT NULL,
    page INTEGER,
    text TEXT NOT NULL,
    UNIQUE (document_id, position)
);

CREATE TABLE chunk_vectors (
    chunk_id INTEGER PRIMARY KEY REFERENCES chunks(id),
    vector BLOB NOT NULL
);

CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE VIRTUAL TABLE chunks_fts USING fts5(
    title,
    section,
    text,
    content = 'chunks',
    content_rowid = 'id',
    tokenize = 'unicode61 remove_diacritics 2'
);
)sql";

void store_documents(Database& database, const std::vector<IndexedDocument>& documents) {
    Statement document_insert(database, R"sql(
        INSERT INTO documents (id, title, issuer, theme, language, document_type, format, url,
                               published, version, version_status, reuse)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    Statement chunk_insert(database, R"sql(
        INSERT INTO chunks (document_id, position, title, section, page, text) VALUES (?, ?, ?, ?, ?, ?)
    )sql");

    Statement vector_insert(database, "INSERT INTO chunk_vectors (chunk_id, vector) VALUES (?, ?)");

    for (const IndexedDocument& document : documents) {
        const DocumentRecord& record = document.record;
        document_insert.bind(1, record.id);
        document_insert.bind(2, record.title);
        document_insert.bind(3, record.issuer);
        document_insert.bind(4, record.theme);
        document_insert.bind(5, record.language);
        document_insert.bind(6, record.document_type);
        document_insert.bind(7, record.format);
        document_insert.bind(8, record.url);
        document_insert.bind(9, record.published);
        document_insert.bind(10, record.version);
        document_insert.bind(11, record.version_status);
        document_insert.bind(12, record.reuse);
        document_insert.run();

        if (document.vectors.size() != document.chunks.size()) {
            throw std::runtime_error("Nombre de vecteurs incorrect pour " + record.id);
        }

        for (std::size_t i = 0; i < document.chunks.size(); ++i) {
            const Chunk& chunk = document.chunks[i];
            chunk_insert.bind(1, record.id);
            chunk_insert.bind(2, chunk.position);
            chunk_insert.bind(3, record.title);
            chunk_insert.bind(4, chunk.section);
            chunk_insert.bind(5, chunk.page);
            chunk_insert.bind(6, chunk.text);
            chunk_insert.run();

            vector_insert.bind(1, static_cast<int>(sqlite3_last_insert_rowid(database.get())));
            vector_insert.bind_blob(2, document.vectors[i].data(), document.vectors[i].size() * sizeof(float));
            vector_insert.run();
        }
    }
}

}  // namespace

void write_index(const std::filesystem::path& database_path, const std::vector<IndexedDocument>& documents,
                 const std::string& embedding_model) {
    std::filesystem::create_directories(database_path.parent_path());

    std::filesystem::path temporary = database_path;
    temporary += ".tmp";
    std::filesystem::remove(temporary);

    try {
        Database database(temporary, Database::Mode::Create);
        database.execute("PRAGMA foreign_keys = ON;");
        database.execute(kSchema);
        database.execute("BEGIN;");
        store_documents(database, documents);
        {
            Statement metadata(database, "INSERT INTO metadata (key, value) VALUES ('embedding_model', ?)");
            metadata.bind(1, embedding_model);
            metadata.run();
        }
        database.execute("INSERT INTO chunks_fts(chunks_fts) VALUES ('rebuild');");
        database.execute("COMMIT;");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }

    std::filesystem::rename(temporary, database_path);
}

}  // namespace vigie::corpus
