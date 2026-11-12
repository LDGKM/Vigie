#include "mcp/client.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vigie::mcp {
namespace {

using Clock = std::chrono::steady_clock;
constexpr const char* kProtocolVersion = "2025-11-25";
constexpr std::chrono::milliseconds kStartTimeout{20000};

}  // namespace

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

}  // namespace

struct Client::Process {
    HANDLE process = nullptr;
    HANDLE input = nullptr;   // Écriture vers l'entrée standard du serveur.
    HANDLE output = nullptr;  // Lecture de sa sortie standard.
    std::string buffer;

    explicit Process(const std::string& command) {
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE child_input = nullptr;
        HANDLE child_output = nullptr;
        if (!CreatePipe(&child_input, &input, &inherit, 0) || !CreatePipe(&output, &child_output, &inherit, 0)) {
            throw ClientError("Impossible de créer les tubes du serveur MCP");
        }
        // Seules les extrémités du serveur sont héritées.
        SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = child_input;
        startup.hStdOutput = child_output;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);  // Ses journaux s'affichent avec ceux de Vigie.

        PROCESS_INFORMATION information{};
        std::wstring line = widen(command);
        const BOOL created = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                            nullptr, &startup, &information);
        CloseHandle(child_input);
        CloseHandle(child_output);
        if (!created) {
            CloseHandle(input);
            CloseHandle(output);
            throw ClientError("Impossible de lancer le serveur MCP (erreur " + std::to_string(GetLastError()) +
                              ") : " + command);
        }
        CloseHandle(information.hThread);
        process = information.hProcess;
    }

    ~Process() {
        CloseHandle(input);  // Fin de l'entrée standard : le serveur s'arrête de lui-même.
        if (WaitForSingleObject(process, 1000) == WAIT_TIMEOUT) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 1000);
        }
        CloseHandle(output);
        CloseHandle(process);
    }

    void write_line(const std::string& line) {
        const std::string data = line + '\n';
        std::size_t written_total = 0;
        while (written_total < data.size()) {
            DWORD written = 0;
            if (!WriteFile(input, data.data() + written_total, static_cast<DWORD>(data.size() - written_total),
                           &written, nullptr)) {
                throw ClientError("Le serveur MCP ne lit plus ses messages");
            }
            written_total += written;
        }
    }

    // Lit une ligne complète avant l'échéance ; renvoie false si le délai est dépassé.
    bool read_line(std::string& line, Clock::time_point deadline) {
        while (true) {
            const std::size_t end = buffer.find('\n');
            if (end != std::string::npos) {
                line = buffer.substr(0, end);
                buffer.erase(0, end + 1);
                return true;
            }
            DWORD available = 0;
            if (!PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr)) {
                throw ClientError("Le serveur MCP s'est arrêté");
            }
            if (available == 0) {
                if (Clock::now() >= deadline) {
                    return false;
                }
                Sleep(5);
                continue;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!ReadFile(output, chunk.data(), available, &read, nullptr) || read == 0) {
                throw ClientError("Le serveur MCP s'est arrêté");
            }
            buffer.append(chunk.data(), read);
        }
    }
};

#else

namespace {

// Découpe une ligne de commande en arguments ; les guillemets regroupent un chemin avec espaces.
std::vector<std::string> split_command(const std::string& command) {
    std::vector<std::string> arguments;
    std::string current;
    bool quoted = false;
    bool started = false;
    for (const char character : command) {
        if (character == '"') {
            quoted = !quoted;
            started = true;
        } else if ((character == ' ' || character == '\t') && !quoted) {
            if (started) {
                arguments.push_back(std::move(current));
                current.clear();
                started = false;
            }
        } else {
            current += character;
            started = true;
        }
    }
    if (started) {
        arguments.push_back(std::move(current));
    }
    return arguments;
}

}  // namespace

struct Client::Process {
    pid_t pid = -1;
    int input = -1;
    int output = -1;
    std::string buffer;

    explicit Process(const std::string& command) {
        const std::vector<std::string> arguments = split_command(command);
        if (arguments.empty()) {
            throw ClientError("Commande du serveur MCP vide");
        }
        int to_child[2];
        int from_child[2];
        if (pipe(to_child) != 0 || pipe(from_child) != 0) {
            throw ClientError("Impossible de créer les tubes du serveur MCP");
        }
        std::signal(SIGPIPE, SIG_IGN);  // Une écriture vers un serveur arrêté lève une erreur, pas un signal.

        pid = fork();
        if (pid < 0) {
            throw ClientError("Impossible de lancer le serveur MCP");
        }
        if (pid == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            close(to_child[0]);
            close(to_child[1]);
            close(from_child[0]);
            close(from_child[1]);
            std::vector<char*> argv;
            for (const std::string& argument : arguments) {
                argv.push_back(const_cast<char*>(argument.c_str()));
            }
            argv.push_back(nullptr);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        input = to_child[1];
        output = from_child[0];
    }

    ~Process() {
        close(input);  // Fin de l'entrée standard : le serveur s'arrête de lui-même.
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (waitpid(pid, nullptr, WNOHANG) == pid) {
                close(output);
                return;
            }
            usleep(10000);
        }
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        close(output);
    }

    void write_line(const std::string& line) {
        const std::string data = line + '\n';
        std::size_t written_total = 0;
        while (written_total < data.size()) {
            const ssize_t written = write(input, data.data() + written_total, data.size() - written_total);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw ClientError("Le serveur MCP ne lit plus ses messages");
            }
            written_total += static_cast<std::size_t>(written);
        }
    }

    bool read_line(std::string& line, Clock::time_point deadline) {
        while (true) {
            const std::size_t end = buffer.find('\n');
            if (end != std::string::npos) {
                line = buffer.substr(0, end);
                buffer.erase(0, end + 1);
                return true;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            if (remaining.count() <= 0) {
                return false;
            }
            pollfd watched{output, POLLIN, 0};
            const int ready = poll(&watched, 1, static_cast<int>(remaining.count()));
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            if (ready <= 0) {
                return false;
            }
            char chunk[4096];
            const ssize_t read_count = read(output, chunk, sizeof(chunk));
            if (read_count <= 0) {
                throw ClientError("Le serveur MCP s'est arrêté");
            }
            buffer.append(chunk, static_cast<std::size_t>(read_count));
        }
    }
};

#endif

Client::Client(std::string command) : command_(std::move(command)) {}

Client::~Client() {
    const std::lock_guard lock(mutex_);
    stop_locked();
}

void Client::start() {
    const std::lock_guard lock(mutex_);
    start_locked();
}

void Client::start_locked() {
    if (process_) {
        return;
    }
    process_ = std::make_unique<Process>(command_);
    try {
        request_locked("initialize",
                       {{"protocolVersion", kProtocolVersion},
                        {"capabilities", nlohmann::json::object()},
                        {"clientInfo", {{"name", "vigie"}, {"version", "1.0"}}}},
                       kStartTimeout);
        process_->write_line(nlohmann::json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}.dump());
    } catch (...) {
        stop_locked();
        throw;
    }
}

void Client::stop_locked() {
    process_.reset();
}

nlohmann::json Client::request_locked(const std::string& method, const nlohmann::json& params,
                                      std::chrono::milliseconds timeout) {
    const long long id = next_id_++;
    process_->write_line(
        nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}}.dump());

    const Clock::time_point deadline = Clock::now() + timeout;
    std::string line;
    while (true) {
        if (!process_->read_line(line, deadline)) {
            // Une réponse tardive décalerait toutes les suivantes : on repart d'un processus neuf.
            stop_locked();
            throw ClientError("Le serveur MCP n'a pas répondu à temps (" + method + ")");
        }
        const nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            continue;  // Ligne hors protocole : ignorée.
        }
        if (!message.contains("id") || message["id"] != id) {
            continue;  // Notification du serveur ou réponse à une autre requête.
        }
        if (message.contains("error")) {
            throw ClientError("Erreur MCP : " + message["error"].value("message", std::string("inconnue")));
        }
        return message.value("result", nlohmann::json::object());
    }
}

nlohmann::json Client::call_tool(const std::string& name, const nlohmann::json& arguments,
                                 std::chrono::milliseconds timeout) {
    const std::lock_guard lock(mutex_);
    nlohmann::json result;
    try {
        start_locked();
        result = request_locked("tools/call", {{"name", name}, {"arguments", arguments}}, timeout);
    } catch (const std::exception& error) {
        // Tube fermé, serveur arrêté, délai dépassé : le prochain appel relancera le serveur.
        stop_locked();
        throw ClientError(error.what());
    }
    if (result.value("isError", false)) {
        std::string text = "erreur de l'outil";
        if (result.contains("content") && result["content"].is_array() && !result["content"].empty()) {
            text = result["content"][0].value("text", text);
        }
        throw ClientError(name + " : " + text);
    }
    if (!result.contains("structuredContent") || !result["structuredContent"].is_object()) {
        throw ClientError(name + " : réponse sans contenu structuré");
    }
    return result["structuredContent"];
}

}  // namespace vigie::mcp
