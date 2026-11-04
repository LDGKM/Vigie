#include "core/assistant.hpp"

namespace vigie {

Answer Assistant::answer(const std::string& /*question*/) const {
    return Answer{
        "Le moteur RAG n'est pas encore branché : aucune recherche n'a été effectuée.",
        {},
        "demo",
    };
}

}  // namespace vigie
