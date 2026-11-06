#include "llm/local_generator.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <llama.h>

namespace vigie::llm {
namespace {

constexpr std::uint32_t kContextTokens = 4096;
constexpr int kMaximumAnswerTokens = 320;

void ignore_llama_logs(ggml_log_level, const char*, void*) {}

void initialize_backend() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(ignore_llama_logs, nullptr);
        llama_backend_init();
    });
}

int thread_count() {
    return std::max(2, static_cast<int>(std::thread::hardware_concurrency()) / 2);
}

std::string conversation(const std::string& system_prompt, const std::string& user_prompt) {
    return "<|im_start|>system\n" + system_prompt + "<|im_end|>\n<|im_start|>user\n" + user_prompt +
           "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";
}

}  // namespace

class LocalGenerator::Impl {
public:
    explicit Impl(const std::filesystem::path& model_path) : name_(model_path.filename().string()) {
        initialize_backend();

        const auto path = model_path.u8string();
        const std::string utf8(reinterpret_cast<const char*>(path.data()), path.size());

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;
        model_ = llama_model_load_from_file(utf8.c_str(), model_params);
        if (model_ == nullptr) {
            throw std::runtime_error("Modele de langage illisible : " + model_path.string());
        }

        llama_context_params context_params = llama_context_default_params();
        context_params.n_ctx = kContextTokens;
        context_params.n_batch = kContextTokens;
        context_params.n_threads = thread_count();
        context_params.n_threads_batch = thread_count();

        context_ = llama_init_from_model(model_, context_params);
        if (context_ == nullptr) {
            llama_model_free(model_);
            throw std::runtime_error("Initialisation du modele de langage impossible");
        }

        vocabulary_ = llama_model_get_vocab(model_);

        // Le choix le plus probable à chaque mot rend les réponses reproductibles.
        sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
    }

    ~Impl() {
        llama_sampler_free(sampler_);
        llama_free(context_);
        llama_model_free(model_);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    std::string generate(const std::string& system_prompt, const std::string& user_prompt) {
        std::vector<llama_token> tokens = tokenize(conversation(system_prompt, user_prompt));
        if (tokens.size() + kMaximumAnswerTokens > kContextTokens) {
            throw std::runtime_error("Passages trop longs pour le modele de langage");
        }

        const std::lock_guard lock(mutex_);
        llama_memory_clear(llama_get_memory(context_), true);
        llama_sampler_reset(sampler_);

        llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<std::int32_t>(tokens.size()));
        if (llama_decode(context_, batch) != 0) {
            throw std::runtime_error("Lecture du prompt impossible");
        }

        std::string answer;
        for (int count = 0; count < kMaximumAnswerTokens; ++count) {
            llama_token token = llama_sampler_sample(sampler_, context_, -1);
            if (llama_vocab_is_eog(vocabulary_, token)) {
                return answer;
            }

            answer += piece(token);
            batch = llama_batch_get_one(&token, 1);
            if (llama_decode(context_, batch) != 0) {
                throw std::runtime_error("Redaction interrompue");
            }
        }

        // Une réponse coupée pourrait perdre sa condition ou sa citation : mieux vaut ne pas l'afficher.
        throw std::runtime_error("Reponse trop longue");
    }

    const std::string& name() const { return name_; }

private:
    std::vector<llama_token> tokenize(const std::string& text) const {
        std::vector<llama_token> tokens(text.size() + 16);
        const std::int32_t count =
            llama_tokenize(vocabulary_, text.c_str(), static_cast<std::int32_t>(text.size()), tokens.data(),
                           static_cast<std::int32_t>(tokens.size()), true, true);
        if (count < 0) {
            throw std::runtime_error("Decoupage du prompt impossible");
        }
        tokens.resize(static_cast<std::size_t>(count));
        return tokens;
    }

    std::string piece(llama_token token) const {
        std::string text(64, '\0');
        std::int32_t size = llama_token_to_piece(vocabulary_, token, text.data(),
                                                 static_cast<std::int32_t>(text.size()), 0, false);
        if (size < 0) {
            text.resize(static_cast<std::size_t>(-size));
            size = llama_token_to_piece(vocabulary_, token, text.data(), static_cast<std::int32_t>(text.size()), 0,
                                        false);
        }
        text.resize(static_cast<std::size_t>(std::max(size, 0)));
        return text;
    }

    std::string name_;
    llama_model* model_ = nullptr;
    llama_context* context_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    const llama_vocab* vocabulary_ = nullptr;
    std::mutex mutex_;
};

LocalGenerator::LocalGenerator(const std::filesystem::path& model_path) : impl_(std::make_unique<Impl>(model_path)) {}

LocalGenerator::~LocalGenerator() = default;

std::string LocalGenerator::generate(const std::string& system_prompt, const std::string& user_prompt) const {
    return impl_->generate(system_prompt, user_prompt);
}

std::string LocalGenerator::name() const {
    return impl_->name();
}

}  // namespace vigie::llm
