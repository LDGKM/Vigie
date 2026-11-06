#include "api/routes.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace vigie {
namespace {

constexpr std::size_t kMaxQuestionBytes = 4000;

crow::response json_response(int status, const crow::json::wvalue& body) {
    crow::response response(status, body.dump());
    response.set_header("Content-Type", "application/json; charset=utf-8");
    return response;
}

crow::response error_response(int status, const std::string& code, const std::string& message) {
    crow::json::wvalue body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    return json_response(status, body);
}

template <typename T>
void set_optional(crow::json::wvalue& item, const std::string& key, const std::optional<T>& value) {
    if (value) {
        item[key] = *value;
    } else {
        item[key] = nullptr;
    }
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

crow::response serve_index(const std::string& web_dir) {
    const std::filesystem::path path = std::filesystem::path(web_dir) / "index.html";
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return error_response(500, "web_not_found", "La page web est introuvable.");
    }

    std::ostringstream content;
    content << file.rdbuf();

    crow::response response(200, content.str());
    response.set_header("Content-Type", "text/html; charset=utf-8");
    return response;
}

crow::response handle_ask(const crow::request& request, const Assistant& assistant) {
    const auto body = crow::json::load(request.body);
    if (!body || body.t() != crow::json::type::Object) {
        return error_response(400, "invalid_json", "Le corps de la requête doit être un objet JSON.");
    }

    if (!body.has("question") || body["question"].t() != crow::json::type::String) {
        return error_response(400, "missing_question", "Le champ \"question\" est obligatoire.");
    }

    const std::string raw_question = body["question"].s();
    const std::string question = trim(raw_question);
    if (question.empty()) {
        return error_response(400, "empty_question", "La question est vide.");
    }
    if (question.size() > kMaxQuestionBytes) {
        return error_response(413, "question_too_long", "La question est trop longue.");
    }

    Answer answer;
    try {
        answer = assistant.answer(question);
    } catch (const corpus::IndexUnavailable&) {
        return error_response(503, "index_unavailable",
                              "La base documentaire n'est pas prête : lance vigie_index, puis réessaie.");
    } catch (const std::exception& error) {
        CROW_LOG_ERROR << "Recherche impossible : " << error.what();
        return error_response(500, "search_failed", "La recherche a échoué.");
    }

    crow::json::wvalue response;
    response["answer"] = answer.text;
    response["engine"] = answer.engine;
    response["model"] = answer.model;
    response["status"] = answer.status;

    crow::json::wvalue::list sources;
    for (const Source& source : answer.sources) {
        crow::json::wvalue item;
        item["number"] = source.number;
        item["document_id"] = source.document_id;
        item["title"] = source.title;
        item["issuer"] = source.issuer;
        item["section"] = source.section;
        set_optional(item, "page", source.page);
        item["excerpt"] = source.excerpt;
        item["url"] = source.url;
        set_optional(item, "published", source.published);
        set_optional(item, "version", source.version);
        item["version_status"] = source.version_status;
        sources.push_back(std::move(item));
    }
    response["sources"] = std::move(sources);

    return json_response(200, response);
}

}  // namespace

void register_routes(crow::SimpleApp& app, const Config& config, const Assistant& assistant) {
    // Page web de l'assistant.
    CROW_ROUTE(app, "/").methods(crow::HTTPMethod::GET)([web_dir = config.web_dir] {
        return serve_index(web_dir);
    });

    // Vérifie que le serveur fonctionne.
    CROW_ROUTE(app, "/health").methods(crow::HTTPMethod::GET)([] {
        crow::json::wvalue body;
        body["status"] = "ok";
        body["service"] = "vigie";
        body["version"] = VIGIE_VERSION;
        return json_response(200, body);
    });

    // Reçoit une question et renvoie une réponse avec ses sources.
    CROW_ROUTE(app, "/ask").methods(crow::HTTPMethod::POST)([&assistant](const crow::request& request) {
        return handle_ask(request, assistant);
    });
}

}  // namespace vigie
