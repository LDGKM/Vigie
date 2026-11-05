#include <exception>
#include <iostream>

#include <crow.h>

#include "api/routes.hpp"
#include "config.hpp"
#include "core/assistant.hpp"

int main() {
    try {
        const vigie::Config config = vigie::load_config();
        const vigie::Assistant assistant{vigie::corpus::SearchIndex(config.database_path)};

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
