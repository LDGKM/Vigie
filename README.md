# Vigie

Assistant conversationnel de finance numérique fondé sur une architecture RAG, écrit en C++20.

**État actuel :** le serveur, la page web et l'API fonctionnent. Le moteur RAG n'est pas encore branché : `/ask` renvoie une réponse de démonstration, sans source.

## Compiler et lancer dans Visual Studio

Prérequis : Visual Studio avec la charge de travail **Développement Desktop en C++** (qui inclut les outils CMake) et Git. Une connexion Internet est nécessaire à la première configuration pour télécharger Crow et Asio.

1. Ouvre Visual Studio, puis choisis **Ouvrir un dossier local** et sélectionne ce dossier.
2. Attends la fin de la configuration CMake et sélectionne le préréglage **Windows x64 Debug**.
3. Choisis `vigie.exe` comme élément de démarrage, puis lance-le avec **F5**.
4. Ouvre <http://127.0.0.1:8080> dans ton navigateur.

## Tester l'API

Dans PowerShell, pendant que le serveur tourne :

```powershell
Invoke-RestMethod http://127.0.0.1:8080/health

Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8080/ask `
  -ContentType "application/json; charset=utf-8" `
  -Body '{"question":"Quel est le rôle de MiCA ?"}'
```

| Méthode | Route | Rôle |
|---|---|---|
| GET | `/` | Page web de l'assistant |
| GET | `/health` | Vérifie que le serveur fonctionne |
| POST | `/ask` | Reçoit `{"question": "..."}` et renvoie `answer`, `sources` et `engine` |

En cas d'erreur, `/ask` renvoie un objet `{"error": {"code": "...", "message": "..."}}`, avec le statut 400 ou 413.

## Configuration

| Variable d'environnement | Valeur par défaut | Rôle |
|---|---|---|
| `VIGIE_PORT` | `8080` | Port du serveur |
| `VIGIE_WEB_DIR` | dossier `web/` du projet | Emplacement de la page web |

Le serveur n'écoute que sur `127.0.0.1` : il n'est accessible que depuis ce PC.

## Structure

```text
src/main.cpp            Démarrage du serveur
src/config.*            Lecture de la configuration
src/api/routes.*        Routes HTTP et validation des requêtes
src/core/assistant.*    Moteur de l'assistant, pour l'instant en démonstration
web/index.html          Page web
data/corpus/            Futurs documents de la base documentaire
```

Les routes dépendent uniquement de la classe `Assistant`. La recherche et le modèle de langage pourront donc être ajoutés sans modifier l'API.
