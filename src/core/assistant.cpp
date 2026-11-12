#include "core/assistant.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "core/followup.hpp"
#include "core/grounding.hpp"
#include "core/smalltalk.hpp"

namespace vigie {
namespace {

constexpr std::size_t kPassagesForAnswer = 3;
constexpr const char* kNotFound = "Information non trouvée dans la base documentaire de Vigie.";

Source to_source(const corpus::SearchHit& hit, int number) {
    return {number,         hit.document_id, hit.title,     hit.issuer,  hit.section,       hit.page,
            hit.text,       hit.url,         hit.published, hit.version, hit.version_status};
}

std::vector<Source> to_sources(const std::vector<corpus::SearchHit>& hits) {
    std::vector<Source> sources;
    for (std::size_t index = 0; index < hits.size(); ++index) {
        sources.push_back(to_source(hits[index], static_cast<int>(index + 1)));
    }
    return sources;
}

// Date ISO 8601 « 2026-11-12T16:20:05+01:00 » écrite « 12/11/2026 à 16 h 20 ».
std::string french_timestamp(const std::string& iso) {
    if (iso.size() < 16) {
        return iso;
    }
    return iso.substr(8, 2) + '/' + iso.substr(5, 2) + '/' + iso.substr(0, 4) + " à " + iso.substr(11, 2) + " h " +
           iso.substr(14, 2);
}

std::string string_field(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

// Le passage renvoyé par vigie-donnees devient un passage comme ceux du corpus :
// il est cité, affiché et contrôlé de la même façon.
corpus::SearchHit tool_passage(const std::string& tool, const nlohmann::json& passage) {
    corpus::SearchHit hit;
    hit.document_id = "donnees:" + tool;
    hit.title = string_field(passage, "titre");
    hit.issuer = string_field(passage, "emetteur");
    hit.url = string_field(passage, "url");
    hit.text = string_field(passage, "texte");
    hit.section = "Donnée consultée le " + french_timestamp(string_field(passage, "date_consultation"));
    hit.version_status = "donnée en direct";
    if (const std::string date = string_field(passage, "date_donnee"); !date.empty()) {
        hit.published = date;
    }
    return hit;
}

}  // namespace

Assistant::Assistant(corpus::SearchIndex index, const llm::Generator* generator, mcp::Client* tools)
    : index_(std::move(index)), generator_(generator), tools_(tools) {}

std::string Assistant::standalone_question(const std::string& question, const std::string& previous_question) const {
    if (previous_question.empty() || !may_depend_on_previous(question)) {
        return question;
    }
    if (generator_ != nullptr) {
        try {
            const std::string raw =
                generator_->generate(followup_system_prompt(), followup_user_prompt(previous_question, question));
            if (const auto rewritten = clean_rewrite(raw)) {
                return *rewritten;
            }
        } catch (const std::exception& error) {
            std::cerr << "Reformulation impossible : " << error.what() << std::endl;
        }
    }
    // Sans reformulation sûre, la recherche reçoit les deux questions.
    return previous_question + " " + question;
}

Answer Assistant::answer(const std::string& asked, const PassagesFound& on_passages,
                         const std::string& previous_question) const {
    if (const auto reply = small_talk(asked)) {
        const bool plan = reply->kind == ChatKind::PlanOffer;
        return Answer{reply->text, {}, "chat", "", plan ? "plan" : "chat", asked};
    }
    const std::string question = standalone_question(without_greeting(asked), previous_question);

    if (tools_ != nullptr) {
        const ToolRoute route = route_question(question);
        if (route.kind == ToolRoute::Kind::Ask) {
            return Answer{route.message, {}, "donnees", "", "chat", question};
        }
        if (route.kind == ToolRoute::Kind::Call) {
            try {
                Answer answer = answer_with_tool(question, route, on_passages);
                answer.question = question;
                return answer;
            } catch (const std::exception& error) {
                // Serveur absent ou en panne : les documents prennent le relais.
                std::cerr << "Donnees en direct indisponibles : " << error.what() << std::endl;
            }
        }
    }

    Answer answer = answer_standalone(question, on_passages);
    answer.question = question;
    return answer;
}

Answer Assistant::answer_standalone(const std::string& question, const PassagesFound& on_passages) const {
    std::vector<corpus::SearchHit> hits = index_.search(question);
    const std::string engine = index_.uses_reranking() ? "reranked" : index_.uses_semantic_search() ? "hybrid" : "keywords";

    if (hits.empty()) {
        return Answer{kNotFound, {}, engine, "", "not_found", {}};
    }

    if (generator_ == nullptr) {
        return Answer{"Voici les passages les plus proches de ta question. Ils sont cités tels quels, "
                      "sans rédaction par une IA.",
                      to_sources(hits), engine, "", "found", {}};
    }

    hits.resize(std::min(hits.size(), kPassagesForAnswer));
    const std::string model = generator_->name();
    if (on_passages) {
        on_passages(to_sources(hits));
    }

    std::string text;
    CheckedAnswer checked;
    try {
        text = generator_->generate(grounded_system_prompt(), grounded_user_prompt(question, hits));
        checked = check_answer(text, hits);
    } catch (const std::exception& error) {
        std::cerr << "Redaction impossible : " << error.what() << std::endl;
    }

    if (checked.check == AnswerCheck::NotFound) {
        return Answer{kNotFound, {}, engine, model, "not_found", {}};
    }

    if (checked.check == AnswerCheck::Unconfirmed) {
        // Un texte non vérifié ne s'affiche pas : les passages d'origine restent consultables.
        std::cerr << "Reponse rejetee : " << text << std::endl;
        return Answer{"Je n'ai pas pu confirmer une réponse rédigée. Voici les passages les plus proches, "
                      "cités tels quels.",
                      to_sources(hits), engine, model, "unconfirmed", {}};
    }

    Answer answer{std::move(checked.text), {}, engine, model, "found", {}};
    for (const int number : checked.citations) {
        answer.sources.push_back(to_source(hits[static_cast<std::size_t>(number - 1)], number));
    }
    return answer;
}

Answer Assistant::answer_with_tool(const std::string& question, const ToolRoute& route,
                                   const PassagesFound& on_passages) const {
    // Les calculs sont immédiats ; une source officielle peut demander plusieurs téléchargements.
    const bool local = route.tool == "mensualite_credit";
    const nlohmann::json passage =
        tools_->call_tool(route.tool, route.arguments, std::chrono::seconds(local ? 5 : 30));

    const corpus::SearchHit hit = tool_passage(route.tool, passage);
    if (hit.text.empty()) {
        throw mcp::ClientError(route.tool + " : passage vide");
    }
    const std::vector<corpus::SearchHit> hits{hit};
    const std::string status = string_field(passage, "statut");

    // Le texte de l'outil est déjà une réponse sourcée, écrite avec soin : on le cite tel quel,
    // sauf pour les livrets, dont le passage couvre cinq produits et mérite d'être résumé.
    Answer verbatim{hit.text + " [1]", to_sources(hits), "donnees", "", "found", {}};
    if (status != "ok" || route.tool != "taux_livrets" || generator_ == nullptr) {
        return verbatim;
    }

    if (on_passages) {
        on_passages(to_sources(hits));
    }
    try {
        const std::string text = generator_->generate(grounded_system_prompt(), grounded_user_prompt(question, hits));
        CheckedAnswer checked = check_answer(text, hits);
        if (checked.check == AnswerCheck::Valid) {
            return Answer{std::move(checked.text), to_sources(hits), "donnees", generator_->name(), "found", {}};
        }
        std::cerr << "Reponse sur donnees rejetee : " << text << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "Redaction impossible : " << error.what() << std::endl;
    }
    return verbatim;
}

}  // namespace vigie
