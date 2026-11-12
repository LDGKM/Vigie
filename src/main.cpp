#include <exception>
#include <iostream>
#include <memory>

#include <crow.h>

#include "api/routes.hpp"
#include "config.hpp"
#include "core/assistant.hpp"
#include "corpus/embedder.hpp"
#include "corpus/reranker.hpp"
#include "llm/local_generator.hpp"

int main() {
    try {
        const vigie::Config config = vigie::load_config();

        // Sans modèle, Vigie reste utilisable avec la recherche par mots-clés.
        std::unique_ptr<vigie::corpus::Embedder> embedder;
        try {
            embedder = std::make_unique<vigie::corpus::Embedder>(config.model_path);
        } catch (const std::exception& error) {
            std::cerr << "Recherche par le sens desactivee : " << error.what() << std::endl;
        }

        std::unique_ptr<vigie::corpus::Reranker> reranker;
        if (embedder) {
            try {
                reranker = std::make_unique<vigie::corpus::Reranker>(config.reranker_path);
            } catch (const std::exception& error) {
                std::cerr << "Reclassement desactive : " << error.what() << std::endl;
            }
        }

        std::unique_ptr<vigie::llm::LocalGenerator> generator;
        try {
            generator = std::make_unique<vigie::llm::LocalGenerator>(config.llm_path);
        } catch (const std::exception& error) {
            std::cerr << "Redaction desactivee : " << error.what() << std::endl;
        }

        // Données officielles en direct et calculs : facultatifs, Vigie s'en passe sans erreur.
        std::unique_ptr<vigie::mcp::Client> data_server;
        if (!config.data_server_command.empty()) {
            data_server = std::make_unique<vigie::mcp::Client>(config.data_server_command);
            try {
                data_server->start();
                std::cout << "Donnees en direct : " << config.data_server_command << std::endl;
            } catch (const std::exception& error) {
                std::cerr << "Donnees en direct desactivees : " << error.what() << std::endl;
                data_server.reset();
            }
        } else {
            std::cout << "Donnees en direct : serveur vigie-donnees non trouve" << std::endl;
        }

        const vigie::Assistant assistant{
            vigie::corpus::SearchIndex(config.database_path, embedder.get(), reranker.get()), generator.get(),
            data_server.get()};

        crow::SimpleApp app;
        vigie::register_routes(app, config, assistant);

        std::cout << "Vigie : http://" << config.host << ':' << config.port << std::endl;
        app.bindaddr(config.host).port(config.port).multithreaded().run();
    } catch (const std::exception& error) {
        std::cerr << "Erreur : " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
