#include "core/assistant.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "core/grounding.hpp"

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

}  // namespace

Assistant::Assistant(corpus::SearchIndex index, const llm::Generator* generator)
    : index_(std::move(index)), generator_(generator) {}

Answer Assistant::answer(const std::string& question, const PassagesFound& on_passages) const {
    std::vector<corpus::SearchHit> hits = index_.search(question);
    const std::string engine = index_.uses_reranking() ? "reranked" : index_.uses_semantic_search() ? "hybrid" : "keywords";

    if (hits.empty()) {
        return Answer{kNotFound, {}, engine, "", "not_found"};
    }

    if (generator_ == nullptr) {
        return Answer{"Voici les passages les plus proches de ta question. Ils sont cités tels quels, "
                      "sans rédaction par une IA.",
                      to_sources(hits), engine, "", "found"};
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
        return Answer{kNotFound, {}, engine, model, "not_found"};
    }

    if (checked.check == AnswerCheck::Unconfirmed) {
        // Un texte non vérifié ne s'affiche pas : les passages d'origine restent consultables.
        std::cerr << "Reponse rejetee : " << text << std::endl;
        return Answer{"Je n'ai pas pu confirmer une réponse rédigée. Voici les passages les plus proches, "
                      "cités tels quels.",
                      to_sources(hits), engine, model, "unconfirmed"};
    }

    Answer answer{std::move(checked.text), {}, engine, model, "found"};
    for (const int number : checked.citations) {
        answer.sources.push_back(to_source(hits[static_cast<std::size_t>(number - 1)], number));
    }
    return answer;
}

}  // namespace vigie
