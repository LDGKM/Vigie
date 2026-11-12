#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <json.hpp>

namespace vigie::mcp {

// Erreur de dialogue avec le serveur : processus absent, délai dépassé, réponse illisible.
class ClientError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Client MCP minimal sur l'entrée et la sortie standard d'un sous-processus (JSON-RPC 2.0,
// un message par ligne). Il lance le serveur au premier appel, puis le garde ouvert.
// Les appels sont faits l'un après l'autre : le serveur HTTP de Vigie peut l'utiliser
// depuis plusieurs fils d'exécution.
class Client {
public:
    // command : ligne de commande complète du serveur, par exemple le chemin de vigie-donnees.exe.
    explicit Client(std::string command);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Lance le serveur et fait la poignée de main initialize, si ce n'est pas déjà fait.
    void start();

    // Appelle un outil et renvoie son structuredContent.
    // Lève ClientError en cas d'échec de transport ou si l'outil signale une erreur (isError).
    // Après un délai dépassé, le processus est arrêté puis relancé au prochain appel.
    nlohmann::json call_tool(const std::string& name, const nlohmann::json& arguments,
                             std::chrono::milliseconds timeout);

private:
    struct Process;

    void start_locked();
    void stop_locked();
    nlohmann::json request_locked(const std::string& method, const nlohmann::json& params,
                                  std::chrono::milliseconds timeout);

    std::string command_;
    std::unique_ptr<Process> process_;
    std::mutex mutex_;
    long long next_id_ = 1;
};

}  // namespace vigie::mcp
