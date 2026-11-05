#include "core/assistant.hpp"

#include <utility>

namespace vigie {

Assistant::Assistant(corpus::SearchIndex index) : index_(std::move(index)) {}

Answer Assistant::answer(const std::string& question) const {
    const std::vector<corpus::SearchHit> hits = index_.search(question);

    if (hits.empty()) {
        return Answer{"Information non trouvée dans la base documentaire de Vigie.", {}, "search", "not_found"};
    }

    Answer answer;
    answer.text = "Voici les passages les plus proches de ta question. Ils sont cités tels quels, "
                  "sans rédaction par une IA.";
    answer.engine = "search";
    answer.status = "found";

    for (const corpus::SearchHit& hit : hits) {
        answer.sources.push_back({hit.document_id, hit.title, hit.issuer, hit.section, hit.page, hit.text, hit.url,
                                  hit.published, hit.version, hit.version_status});
    }

    return answer;
}

}  // namespace vigie
