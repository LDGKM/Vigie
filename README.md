# Vigie

Assistant conversationnel de finance numérique fondé sur une architecture RAG, écrit en C++20.

**État actuel :** le serveur, la page web, l'API, l'indexation et la recherche hybride (mots-clés et sens) fonctionnent. `/ask` renvoie les passages retrouvés et leurs sources, sans les reformuler : le modèle de langage reste à ajouter.

## Compiler et lancer

Utilise la configuration **Release** : en Debug, la vectorisation est beaucoup trop lente. Dans **Developer PowerShell**, depuis la racine du projet :

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
cmake --preset x64-release
cmake --build out\build\x64-release
.\out\build\x64-release\vigie_tests.exe
.\out\build\x64-release\vigie_index.exe
.\out\build\x64-release\vigie.exe
```

Ouvre ensuite <http://127.0.0.1:8080>.

## Compiler et lancer dans Visual Studio

Prérequis : Visual Studio avec la charge de travail **Développement Desktop en C++** (qui inclut les outils CMake) et Git. Une connexion Internet est nécessaire à la première configuration pour télécharger les dépendances.

1. Ouvre Visual Studio, puis choisis **Ouvrir un dossier local** et sélectionne ce dossier.
2. Attends la fin de la configuration CMake et sélectionne le préréglage **Windows x64 Release**.
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
| POST | `/ask` | Reçoit `{"question": "..."}` et renvoie `answer`, `status`, `sources` et `engine` |

`status` vaut `found` ou `not_found`. En cas d'erreur, `/ask` renvoie un objet `{"error": {"code": "...", "message": "..."}}`, avec le statut 400, 413, 500 ou 503. Le statut 503 signifie que la base doit être créée avec `vigie_index`.

## Configuration

| Variable d'environnement | Valeur par défaut | Rôle |
|---|---|---|
| `VIGIE_PORT` | `8080` | Port du serveur |
| `VIGIE_WEB_DIR` | dossier `web/` du projet | Emplacement de la page web |
| `VIGIE_DB_PATH` | `data/index/vigie.db` | Emplacement de la base documentaire |
| `VIGIE_MODEL_PATH` | `models/multilingual-e5-small-f16.gguf` | Modèle de vectorisation |

Le serveur n'écoute que sur `127.0.0.1` : il n'est accessible que depuis ce PC.

## Corpus

Les fiches des documents sont dans `data/corpus/sources.json`. Les fichiers eux-mêmes ne sont pas publiés sur GitHub. Pour les télécharger dans `data/corpus/raw/` :

```powershell
powershell -ExecutionPolicy Bypass -File scripts\download_corpus.ps1
```

Le script vérifie chaque fichier. S'il signale un échec, ouvre le lien indiqué et enregistre le document à l'emplacement affiché.

Pour créer la base `data/index/vigie.db`, lance `vigie_index.exe` depuis Visual Studio ou :

```powershell
.\out\build\x64-release\vigie_index.exe
```

L'outil lit les 11 documents, les découpe par article, section ou page, calcule leurs vecteurs sur le processeur, puis enregistre le tout. Cette étape prend plusieurs minutes. Si un document pose problème, la base existante n'est pas modifiée. Les tests se lancent avec `.\out\build\x64-release\vigie_tests.exe`.

## Structure

```text
src/main.cpp            Démarrage du serveur
src/config.*            Lecture de la configuration
src/api/routes.*        Routes HTTP et validation des requêtes
src/core/assistant.*    Moteur de l'assistant, pour l'instant en démonstration
src/corpus/             Lecture, découpage, vectorisation, stockage et recherche
src/tools/              Outils d'indexation et de test de la recherche
tests/                  Tests du corpus
web/index.html          Page web
data/corpus/sources.json  Fiches des documents de la base documentaire
scripts/                Outils de préparation du corpus
```

Les routes dépendent uniquement de la classe `Assistant`. La recherche et le modèle de langage pourront donc être ajoutés sans modifier l'API.

## Modèle de vectorisation

Vigie utilise `multilingual-e5-small`, créé par intfloat sous licence MIT, dans [une conversion GGUF publique](https://huggingface.co/hdrrayan/multilingual-e5-small-gguf). Le script `scripts/download_model.ps1` télécharge une révision précise et vérifie son empreinte SHA-256. Si le modèle est absent, le serveur démarre avec la recherche par mots-clés seule.
