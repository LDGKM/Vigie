#include "corpus/embedder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <llama.h>

namespace vigie::corpus {
namespace {

constexpr int kMaximumTokens = 512;

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

class Embedder::Impl {
public:
    explicit Impl(const std::filesystem::path& model_path) {
        initialize_backend();

        const std::u8string name = model_path.u8string();
        const std::string utf8(reinterpret_cast<const char*>(name.data()), name.size());

        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;
        model_ = llama_model_load_from_file(utf8.c_str(), model_params);
        if (model_ == nullptr) {
            throw std::runtime_error("Modele de vectorisation illisible : " + model_path.string());
        }

        llama_context_params context_params = llama_context_default_params();
        context_params.n_ctx = kMaximumTokens;
        context_params.n_batch = kMaximumTokens;
        context_params.n_ubatch = kMaximumTokens;
        context_params.embeddings = true;
        context_params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        context_params.n_threads = thread_count();
        context_params.n_threads_batch = thread_count();

        context_ = llama_init_from_model(model_, context_params);
        if (context_ == nullptr) {
            llama_model_free(model_);
            throw std::runtime_error("Initialisation du modele de vectorisation impossible");
        }

        vocabulary_ = llama_model_get_vocab(model_);
        dimensions_ = static_cast<std::size_t>(llama_model_n_embd(model_));
    }

    ~Impl() {
        llama_free(context_);
        llama_model_free(model_);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    std::size_t dimensions() const { return dimensions_; }

    std::vector<float> embed(const std::string& text) {
        // Un contexte llama.cpp ne peut traiter qu'une demande à la fois.
        const std::lock_guard<std::mutex> lock(mutex_);

        std::vector<llama_token> tokens = tokenize(text);
        llama_memory_clear(llama_get_memory(context_), true);

        const llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        const bool encoder_only = llama_model_has_encoder(model_) && !llama_model_has_decoder(model_);
        const int32_t status = encoder_only ? llama_encode(context_, batch) : llama_decode(context_, batch);
        if (status != 0) {
            throw std::runtime_error("Vectorisation impossible");
        }

        const float* raw = llama_get_embeddings_seq(context_, 0);
        if (raw == nullptr) {
            throw std::runtime_error("Vecteur indisponible");
        }

        std::vector<float> vector(raw, raw + dimensions_);
        normalize(vector);
        return vector;
    }

private:
    std::vector<llama_token> tokenize(const std::string& text) const {
        const auto size = static_cast<int32_t>(text.size());
        std::vector<llama_token> tokens(static_cast<std::size_t>(size) + 8);
        int32_t count = llama_tokenize(vocabulary_, text.data(), size, tokens.data(),
                                       static_cast<int32_t>(tokens.size()), true, false);
        if (count < 0) {
            tokens.resize(static_cast<std::size_t>(-count));
            count = llama_tokenize(vocabulary_, text.data(), size, tokens.data(),
                                   static_cast<int32_t>(tokens.size()), true, false);
        }
        if (count <= 0) {
            throw std::runtime_error("Texte vide");
        }

        tokens.resize(static_cast<std::size_t>(count));
        if (tokens.size() > kMaximumTokens) {
            const llama_token last = tokens.back();
            tokens.resize(kMaximumTokens);
            tokens.back() = last;
        }
        return tokens;
    }

    static void normalize(std::vector<float>& vector) {
        double sum = 0.0;
        for (const float value : vector) {
            sum += static_cast<double>(value) * static_cast<double>(value);
        }
        const auto length = static_cast<float>(std::sqrt(sum));
        if (length > 0.0F) {
            for (float& value : vector) {
                value /= length;
            }
        }
    }

    llama_model* model_ = nullptr;
    llama_context* context_ = nullptr;
    const llama_vocab* vocabulary_ = nullptr;
    std::size_t dimensions_ = 0;
    std::mutex mutex_;
};

Embedder::Embedder(const std::filesystem::path& model_path) : impl_(std::make_unique<Impl>(model_path)) {}
Embedder::~Embedder() = default;

std::size_t Embedder::dimensions() const {
    return impl_->dimensions();
}

std::vector<float> Embedder::embed_query(std::string_view text) const {
    return impl_->embed("query: " + std::string(text));
}

std::vector<float> Embedder::embed_passage(std::string_view text) const {
    return impl_->embed("passage: " + std::string(text));
}

}  // namespace vigie::corpus
