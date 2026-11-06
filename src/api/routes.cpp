#include "api/routes.hpp"

#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vigie {
namespace {

constexpr std::size_t kMaxQuestionBytes = 4000;
constexpr const char* kIndexNotReady = "La base documentaire n'est pas prête : lance vigie_index, puis réessaie.";
constexpr const char* kSearchFailed = "La recherche a échoué.";

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

// Lit la question d'une requête. Renvoie une erreur HTTP si elle n'est pas utilisable.
std::optional<crow::response> read_question(const crow::request& request, std::string& question) {
    const auto body = crow::json::load(request.body);
    if (!body || body.t() != crow::json::type::Object) {
        return error_response(400, "invalid_json", "Le corps de la requête doit être un objet JSON.");
    }

    if (!body.has("question") || body["question"].t() != crow::json::type::String) {
        return error_response(400, "missing_question", "Le champ \"question\" est obligatoire.");
    }

    const std::string raw_question = body["question"].s();
    question = trim(raw_question);
    if (question.empty()) {
        return error_response(400, "empty_question", "La question est vide.");
    }
    if (question.size() > kMaxQuestionBytes) {
        return error_response(413, "question_too_long", "La question est trop longue.");
    }
    return std::nullopt;
}

crow::json::wvalue::list sources_json(const std::vector<Source>& items) {
    crow::json::wvalue::list sources;
    for (const Source& source : items) {
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
    return sources;
}

crow::json::wvalue answer_json(const Answer& answer) {
    crow::json::wvalue response;
    response["answer"] = answer.text;
    response["engine"] = answer.engine;
    response["model"] = answer.model;
    response["status"] = answer.status;
    response["sources"] = sources_json(answer.sources);
    return response;
}

crow::response handle_ask(const crow::request& request, const Assistant& assistant) {
    std::string question;
    if (auto problem = read_question(request, question)) {
        return std::move(*problem);
    }

    try {
        return json_response(200, answer_json(assistant.answer(question)));
    } catch (const corpus::IndexUnavailable&) {
        return error_response(503, "index_unavailable", kIndexNotReady);
    } catch (const std::exception& error) {
        CROW_LOG_ERROR << "Recherche impossible : " << error.what();
        return error_response(500, "search_failed", kSearchFailed);
    }
}

// Une question suivie pas à pas : la page affiche les passages pendant la rédaction.
struct Job {
    std::string state = "searching";  // searching, writing, done ou error.
    std::vector<Source> sources;
    std::optional<Answer> answer;
    std::string error;
};

class Jobs {
public:
    std::string start(const Assistant& assistant, const std::string& question) {
        auto job = std::make_shared<Job>();
        std::string id;
        {
            const std::lock_guard lock(mutex_);
            id = std::to_string(next_id_++);
            jobs_[id] = job;
            order_.push_back(id);
            if (order_.size() > kMaximumJobs) {
                jobs_.erase(order_.front());
                order_.pop_front();
            }
        }

        std::thread([this, &assistant, job, question] { run(assistant, *job, question); }).detach();
        return id;
    }

    std::optional<crow::json::wvalue> status(const std::string& id) {
        const std::lock_guard lock(mutex_);
        const auto found = jobs_.find(id);
        if (found == jobs_.end()) {
            return std::nullopt;
        }

        const Job& job = *found->second;
        crow::json::wvalue body = job.answer ? answer_json(*job.answer) : crow::json::wvalue{};
        body["state"] = job.state;
        if (!job.answer) {
            body["sources"] = sources_json(job.sources);
        }
        if (!job.error.empty()) {
            body["error"]["message"] = job.error;
        }
        return body;
    }

private:
    static constexpr std::size_t kMaximumJobs = 32;

    void run(const Assistant& assistant, Job& job, const std::string& question) {
        const auto on_passages = [&](const std::vector<Source>& sources) {
            const std::lock_guard lock(mutex_);
            job.sources = sources;
            job.state = "writing";
        };

        try {
            Answer answer = assistant.answer(question, on_passages);
            const std::lock_guard lock(mutex_);
            job.answer = std::move(answer);
            job.state = "done";
        } catch (const corpus::IndexUnavailable&) {
            fail(job, kIndexNotReady);
        } catch (const std::exception& error) {
            CROW_LOG_ERROR << "Recherche impossible : " << error.what();
            fail(job, kSearchFailed);
        }
    }

    void fail(Job& job, const std::string& message) {
        const std::lock_guard lock(mutex_);
        job.error = message;
        job.state = "error";
    }

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
    std::deque<std::string> order_;
    unsigned long long next_id_ = 1;
};

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

    // Lance une question en arrière-plan. La page suit ensuite son avancement.
    auto jobs = std::make_shared<Jobs>();
    CROW_ROUTE(app, "/ask/start").methods(crow::HTTPMethod::POST)([&assistant, jobs](const crow::request& request) {
        std::string question;
        if (auto problem = read_question(request, question)) {
            return std::move(*problem);
        }
        crow::json::wvalue body;
        body["job"] = jobs->start(assistant, question);
        return json_response(202, body);
    });

    CROW_ROUTE(app, "/ask/job/<string>").methods(crow::HTTPMethod::GET)([jobs](const std::string& id) {
        if (auto body = jobs->status(id)) {
            return json_response(200, *body);
        }
        return error_response(404, "job_not_found", "Cette question n'est plus suivie.");
    });
}

}  // namespace vigie
