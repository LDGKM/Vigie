#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

namespace vigie::corpus {

// Petites enveloppes qui ferment automatiquement la base et les requêtes.
class Database {
public:
    enum class Mode { ReadOnly, Create };

    Database(const std::filesystem::path& path, Mode mode) {
        const std::u8string name = path.u8string();
        const std::string utf8(reinterpret_cast<const char*>(name.data()), name.size());
        const int flags = mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        if (sqlite3_open_v2(utf8.c_str(), &handle_, flags, nullptr) != SQLITE_OK) {
            const std::string message = handle_ != nullptr ? sqlite3_errmsg(handle_) : "memoire insuffisante";
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

    void bind_blob(int index, const void* data, std::size_t size) {
        sqlite3_bind_blob(handle_, index, data, static_cast<int>(size), SQLITE_TRANSIENT);
    }

    // Renvoie true tant qu'une ligne de résultat est disponible.
    bool step() {
        const int result = sqlite3_step(handle_);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("Erreur SQLite : ") + sqlite3_errmsg(database_.get()));
    }

    void reset() {
        sqlite3_reset(handle_);
        sqlite3_clear_bindings(handle_);
    }

    void run() {
        step();
        reset();
    }

    std::string text(int column) const {
        const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(handle_, column));
        return value != nullptr ? std::string(value) : std::string{};
    }

    std::optional<std::string> optional_text(int column) const {
        if (sqlite3_column_type(handle_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return text(column);
    }

    std::optional<int> optional_int(int column) const {
        if (sqlite3_column_type(handle_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int(handle_, column);
    }

    const void* blob(int column) const { return sqlite3_column_blob(handle_, column); }
    std::size_t blob_size(int column) const { return static_cast<std::size_t>(sqlite3_column_bytes(handle_, column)); }

    int integer(int column) const { return sqlite3_column_int(handle_, column); }
    double real(int column) const { return sqlite3_column_double(handle_, column); }

private:
    Database& database_;
    sqlite3_stmt* handle_ = nullptr;
};

}  // namespace vigie::corpus
