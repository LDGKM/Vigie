#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "llm/generator.hpp"

namespace vigie::llm {

// Modèle de langage local, exécuté sur le processeur avec llama.cpp.
// Le format de conversation est celui de Qwen 3.5, avec le raisonnement désactivé.
class LocalGenerator final : public Generator {
public:
    // Lève std::runtime_error si le modèle est absent ou illisible.
    explicit LocalGenerator(const std::filesystem::path& model_path);
    ~LocalGenerator() override;
    LocalGenerator(const LocalGenerator&) = delete;
    LocalGenerator& operator=(const LocalGenerator&) = delete;

    std::string generate(const std::string& system_prompt, const std::string& user_prompt) const override;
    std::string name() const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vigie::llm
