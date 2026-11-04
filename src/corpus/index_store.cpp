#include "corpus/index_store.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#include <sqlite3.h>

namespace vigie::corpus {
namespace {

constexpr const char* kSchema = R"sql(
PRAGMA user_version = 1;

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
    section TEXT NOT NULL,
    page INTEGER,
    text TEXT NOT NULL,
    UNIQUE (document_id, position)
);

CREATE VIRTUAL TABLE chunks_fts USING fts5(
    section,
    text,
    content = 'chunks',
    content_rowid = 'id',
    tokenize = 'unicode61 remove_diacritics 2'
);
)sql";

class Database {
public:
    explicit Database(const std::filesystem::path& path) {
        const std::u8string name = path.u8string();
        const std::string utf8(reinterpret_cast<const char*>(name.data()), name.size());
        if (sqlite3_open(utf8.c_str(), &handle_) != SQLITE_OK) {
            const std::string message = sqlite3_errmsg(handle_);
            sqlite3_close(handle_);
            throw std::runtime_error("Ouverture de la base impossible : " + message);
        }
    }

    ~Database() { sqlite3_close(handle_); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* get() const { return handle_; }

    void execute(const char* sql) {
        char* error = nullptr;
        if (sqlite3_exec(handle_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message = error != nullptr ? error : "erreur inconnue";
            sqlite3_free(error);
            throw std::runtime_error("Erreur SQLite : " + message);
        }
    }

private:
    sqlite3* handle_ = nullptr;
};

class Statement {
public:
    Statement(Database& database, const char* sql) : database_(database) {
        if (sqlite3_prepare_v2(database.get(), sql, -1, &handle_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("Erreur SQLite : ") + sqlite3_errmsg(database.get()));
        }
    }

    ~Statement() { sqlite3_finalize(handle_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        sqlite3_bind_text(handle_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    void bind(int index, const std::optional<std::string>& value) {
        if (value) {
            bind(index, *value);
        } else {
            sqlite3_bind_null(handle_, index);
        }
    }

    void bind(int index, int value) { sqlite3_bind_int(handle_, index, value); }

    void bind(int index, const std::optional<int>& value) {
        if (value) {
            bind(index, *value);
        } else {
            sqlite3_bind_null(handle_, index);
        }
    }

    void run() {
        if (sqlite3_step(handle_) != SQLITE_DONE) {
            throw std::runtime_error(std::string("Erreur SQLite : ") + sqlite3_errmsg(database_.get()));
        }
        sqlite3_reset(handle_);
        sqlite3_clear_bindings(handle_);
    }

private:
    Database& database_;
    sqlite3_stmt* handle_ = nullptr;
};

void store_documents(Database& database, const std::vector<IndexedDocument>& documents) {
    Statement document_insert(database, R"sql(
        INSERT INTO documents (id, title, issuer, theme, language, document_type, format, url,
                               published, version, version_status, reuse)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql");
    Statement chunk_insert(database, R"sql(
        INSERT INTO chunks (document_id, position, section, page, text) VALUES (?, ?, ?, ?, ?)
    )sql");

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

        for (const Chunk& chunk : document.chunks) {
            chunk_insert.bind(1, record.id);
            chunk_insert.bind(2, chunk.position);
            chunk_insert.bind(3, chunk.section);
            chunk_insert.bind(4, chunk.page);
            chunk_insert.bind(5, chunk.text);
            chunk_insert.run();
        }
    }
}

}  // namespace

void write_index(const std::filesystem::path& database_path, const std::vector<IndexedDocument>& documents) {
    std::filesystem::create_directories(database_path.parent_path());

    std::filesystem::path temporary = database_path;
    temporary += ".tmp";
    std::filesystem::remove(temporary);

    try {
        Database database(temporary);
        database.execute("PRAGMA foreign_keys = ON;");
        database.execute(kSchema);
        database.execute("BEGIN;");
        store_documents(database, documents);
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
