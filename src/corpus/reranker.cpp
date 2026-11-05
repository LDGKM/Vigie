#include "corpus/reranker.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <llama.h>

namespace vigie::corpus {
namespace {

constexpr int kMaximumTokens = 512;
constexpr int kMaximumQuestionTokens = 64;
constexpr std::size_t kMaximumPassageTokens = 256;

void ignore_llama_logs(ggml_log_level, const char*, void*) {}

void initialize_backend() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set(ignore_llama_logs, nullptr);
        llama_backend_init();
    });
}

int thread_count() {
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
}

}  // namespace

class Reranker::Impl {
public:
    explicit Impl(const std::filesystem::path& model_path) {
        initialize_backend();

        const std::u8string name = model_path.u8string();
        const std::string utf8(reinterpret_cast<const char*>(name.data()), name.size());

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;
        model_ = llama_model_load_from_file(utf8.c_str(), model_params);
        if (model_ == nullptr) {
            throw std::runtime_error("Modele de reclassement illisible : " + model_path.string());
        }

        llama_context_params context_params = llama_context_default_params();
        context_params.n_ctx = kMaximumTokens;
        context_params.n_batch = kMaximumTokens;
        context_params.n_ubatch = kMaximumTokens;
        context_params.embeddings = true;
        context_params.pooling_type = LLAMA_POOLING_TYPE_RANK;
        context_params.n_threads = thread_count();
        context_params.n_threads_batch = thread_count();

        context_ = llama_init_from_model(model_, context_params);
        if (context_ == nullptr) {
            llama_model_free(model_);
            throw std::runtime_error("Initialisation du modele de reclassement impossible");
        }
        vocabulary_ = llama_model_get_vocab(model_);
    }

    ~Impl() {
        llama_free(context_);
        llama_model_free(model_);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    float score(std::string_view question, std::string_view passage) {
        const std::lock_guard<std::mutex> lock(mutex_);

        std::vector<llama_token> query = tokenize(question);
        std::vector<llama_token> document = tokenize(passage);
        query.resize(std::min(query.size(), static_cast<std::size_t>(kMaximumQuestionTokens)));

        // Format attendu par ce modèle : début, question, fin, séparateur, passage, fin.
        std::vector<llama_token> tokens;
        const llama_token end = llama_vocab_eos(vocabulary_) != LLAMA_TOKEN_NULL ? llama_vocab_eos(vocabulary_)
                                                                                 : llama_vocab_sep(vocabulary_);
        if (llama_vocab_get_add_bos(vocabulary_)) {
            tokens.push_back(llama_vocab_bos(vocabulary_));
        }
        tokens.insert(tokens.end(), query.begin(), query.end());
        if (llama_vocab_get_add_eos(vocabulary_)) {
            tokens.push_back(end);
        }
        if (llama_vocab_get_add_sep(vocabulary_)) {
            tokens.push_back(llama_vocab_sep(vocabulary_));
        }

        document.resize(std::min(document.size(), kMaximumPassageTokens));
        tokens.insert(tokens.end(), document.begin(), document.end());
        if (llama_vocab_get_add_eos(vocabulary_)) {
            tokens.push_back(end);
        }

        llama_memory_clear(llama_get_memory(context_), true);
        const llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        const bool encoder_only = llama_model_has_encoder(model_) && !llama_model_has_decoder(model_);
        if ((encoder_only ? llama_encode(context_, batch) : llama_decode(context_, batch)) != 0) {
            throw std::runtime_error("Reclassement impossible");
        }

        const float* result = llama_get_embeddings_seq(context_, 0);
        if (result == nullptr) {
            throw std::runtime_error("Score de reclassement indisponible");
        }
        return result[0];
    }

private:
    std::vector<llama_token> tokenize(std::string_view text) const {
        const auto size = static_cast<int32_t>(text.size());
        std::vector<llama_token> tokens(text.size() + 8);
        int32_t count = llama_tokenize(vocabulary_, text.data(), size, tokens.data(),
                                       static_cast<int32_t>(tokens.size()), false, false);
        if (count < 0) {
            tokens.resize(static_cast<std::size_t>(-count));
            count = llama_tokenize(vocabulary_, text.data(), size, tokens.data(),
                                   static_cast<int32_t>(tokens.size()), false, false);
        }
        tokens.resize(static_cast<std::size_t>(std::max(count, 0)));
        return tokens;
    }

    llama_model* model_ = nullptr;
    llama_context* context_ = nullptr;
    const llama_vocab* vocabulary_ = nullptr;
    std::mutex mutex_;
};

Reranker::Reranker(const std::filesystem::path& model_path) : impl_(std::make_unique<Impl>(model_path)) {}
Reranker::~Reranker() = default;

float Reranker::score(std::string_view question, std::string_view passage) const {
    return impl_->score(question, passage);
}

}  // namespace vigie::corpus
