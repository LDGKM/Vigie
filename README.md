# Vigie

Assistant conversationnel de finance numérique fondé sur une architecture RAG, écrit en C++20.

**État actuel :** le serveur, la page web, l'API, l'indexation, la recherche et la rédaction par un modèle local fonctionnent. Chaque réponse rédigée est contrôlée : ses citations doivent exister, et ses nombres doivent figurer dans les passages cités. Sinon, Vigie affiche les passages tels quels.

## Compiler et lancer

Utilise la configuration **Release** : en Debug, la vectorisation est beaucoup trop lente. Dans **Developer PowerShell**, depuis la racine du projet :

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
cmake --preset x64-release
cmake --build out\build\x64-release
.\out\build\x64-release\vigie_tests.exe
.\out\build\x64-release\vigie_index.exe
.\out\build\x64-release\vigie_eval.exe
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
| `VIGIE_MODEL_PATH` | `models/bge-m3-Q4_K_M.gguf` | Modèle de vectorisation |
| `VIGIE_RERANKER_PATH` | `models/bge-reranker-v2-m3-Q4_K_M.gguf` | Modèle de reclassement |
| `VIGIE_LLM_PATH` | `models/Qwen3.5-4B-Q4_K_M.gguf` | Modèle de langage |

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

L'outil lit les 144 documents, les découpe par article, section ou page, calcule leurs vecteurs sur le processeur, puis enregistre le tout. Cette étape prend plusieurs minutes. Si un document pose problème, la base existante n'est pas modifiée. Les tests se lancent avec `.\out\build\x64-release\vigie_tests.exe`.

## Structure

```text
src/main.cpp            Démarrage du serveur
src/config.*            Lecture de la configuration
src/api/routes.*        Routes HTTP et validation des requêtes
src/core/assistant.*    Enchaîne la recherche, la rédaction et le contrôle
src/core/grounding.*    Prépare le prompt et contrôle la réponse
src/llm/                Interface du modèle de langage et modèle local
src/corpus/             Lecture, découpage, vectorisation, stockage et recherche
src/tools/              Outils d'indexation et de test de la recherche
tests/                  Tests du corpus
web/index.html          Page web
data/corpus/sources.json  Fiches des documents de la base documentaire
scripts/                Outils de préparation du corpus
```

Les routes dépendent uniquement de la classe `Assistant`. La recherche et le modèle de langage pourront donc être ajoutés sans modifier l'API.

## Évaluation

`data/eval/questions.json` contient 40 questions couvertes par le corpus et 10 questions hors corpus. Un passage est correct s'il vient du bon document, d'une section acceptée, et contient le texte de preuve. `vigie_eval.exe` mesure le rang du passage correct, les refus et le temps de recherche, puis écrit le détail dans `data/eval/`. Les options `--keywords` et `--no-rerank` permettent de comparer les modes.

`data/eval/validation.json` contient 28 questions couvertes et 8 questions hors corpus, écrites après le choix du modèle et jamais utilisées pour le réglage. Pour les passer : `vigie_eval.exe --questions validation.json`. Les corriger après coup ou régler les seuils dessus les rendrait inutilisables comme contrôle.

### Réponses rédigées

Les 86 questions ont aussi été posées à la chaîne complète, avec `Qwen3.5-4B`, sur 2 cœurs :

- hors corpus : 18 refus sur 18 ;
- dans le corpus : 62 réponses rédigées sur 68, 1 réponse non confirmée (passages affichés) et 5 refus à tort ;
- relecture manuelle des réponses rédigées : 3 fausses et 3 imprécises ;
- temps médian : 52 s pour une réponse, 10 s pour un refus par la recherche.

Le contrôle automatique vérifie les citations et les nombres, pas le sens d'une phrase. C'est pourquoi la page rappelle de vérifier la réponse dans les passages cités.

### Base élargie

La base compte 115 documents et 4 521 passages, avec 104 documents pédagogiques ajoutés : 65 fiches de la Banque de France, 33 fiches de Service Public et 6 guides de l'AMF. `data/eval/definitions.json` contient 28 questions de définition et 8 questions hors sujet, écrites avant la mesure.

- définitions : passage correct parmi les 5 premiers pour 89,3 % des questions, aucune refusée, et 8 refus sur 8 pour les questions hors sujet ;
- anciennes questions : 87,5 % au lieu de 90 % sur le jeu de réglage et 75 % au lieu de 82,1 % sur le jeu de contrôle ;
- une partie de la baisse vient de Service Public, qui répond à des questions dont la référence attend la DSP2. Sur 6 cas relus, 1 réponse est fausse et 3 sont moins précises.

Les fichiers `questions.json` et `validation.json` ne sont pas corrigés. Cinq de leurs questions hors corpus sont désormais couvertes par la base : le livret A, le PEA, la déclaration en ligne, le PEL et l'assurance vie.

### Réponses plus directes

La consigne demande maintenant une réponse dès la première phrase, sans ajout hors sujet, et interdit de mélanger les cas. Le titre du document entre aussi dans le reclassement. Mesures sur la chaîne complète, avec 2 cœurs :

- jeu de contrôle : 26 réponses rédigées sur 28, 1 non confirmée et 1 refusée. La relecture relève 2 réponses fausses et 1 imprécise ;
- définitions : 28 réponses sur 28, dont 1 fausse et 2 faibles ;
- questions hors sujet et demandes de conseil : aucun conseil d'achat. Les 5 questions redevenues couvertes par la base reçoivent une réponse documentaire ;
- temps médian d'une réponse rédigée : 49 s. Recherche seule sur le jeu de contrôle : 78,6 % dans les 5 premiers.

### Recherche et affichage

- Le reclasseur lit maintenant 20 candidats, limités à leurs 600 premiers caractères. Le titre du document fait partie de ce qu'il lit.
- `data/corpus/glossary.json` relie le vocabulaire courant à celui des textes, par exemple « cryptomonnaie » à « crypto-actif ». Il élargit seulement la recherche. `vigie_eval.exe --no-glossary` mesure la recherche sans lui.
- Questions de réglage, passage correct parmi les 5 premiers : 95 % au lieu de 90 %. Définitions : 100 % au lieu de 92,9 %.
- Pour une définition, la réponse explique aussi le fonctionnement ou l'usage, si les extraits le disent.
- La page affiche les passages trouvés dès la fin de la recherche, puis la réponse une fois vérifiée. Le texte n'est jamais montré avant le contrôle. L'API correspondante est `POST /ask/start`, puis `GET /ask/job/<id>`. `POST /ask` reste disponible.

### Conversation

Vigie comprend une question de suite comme « Et pour le PEL ? ». La page envoie la question précédente, telle que Vigie l'a comprise, dans le champ facultatif `previous_question`.

- Une question est traitée comme une question de suite seulement si elle est courte, au plus 12 mots, et si elle commence par une liaison (« et », « mais », « pour »…) ou contient un renvoi (« son », « elle », « ce produit »…). Une nouvelle question comme « C'est quoi la blockchain ? » n'est donc pas modifiée.
- Dans ce cas, le modèle la réécrit en question complète. Une réécriture qui ne ressemble pas à une question est écartée : la recherche reçoit alors les deux questions à la suite.
- La page affiche « Question comprise : … » quand la question a été réécrite. Le bouton « Nouvelle conversation » efface l'historique.
- Une réécriture ajoute environ 10 s ici. Sur 6 échanges testés, les 3 questions de suite ont été bien comprises, la nouvelle question n'a pas été modifiée, et la fausse consigne a été refusée.

### Interface, échanges simples et plan financier

- La page est un fil de discussion. Les citations `[n]` ouvrent la source correspondante, et les passages sont affichés pendant la rédaction.
- Les salutations, remerciements et questions sur Vigie reçoivent une réponse immédiate, sans recherche (`src/core/smalltalk.cpp`, statut `chat`). Une demande de plan financier renvoie le statut `plan`, et la page propose le simulateur.
- Le mode **Plan financier** pose 7 questions (horizon, âge, revenus, dépenses, épargne, versement mensuel, objectifs), puis appelle `POST /plan`. Le calcul est fait en C++ (`src/plan/planner.cpp`), mois par mois, selon trois hypothèses de rendement choisies par l'utilisateur. Il donne l'évolution année par année, l'effet de l'inflation, l'état de chaque objectif et l'épargne mensuelle qui les financerait tous.
- Après le plan, la page répond à « Combien j'aurai dans 5 ans ? », « Quand atteindrai-je 50 000 € ? » ou « Mes objectifs sont-ils atteints ? » à partir du calcul.
- Le simulateur ne recommande aucun produit et ne prévoit aucun rendement.

### Rapport

`docs/rapport/rapport.tex` décrit l'évolution de la base, la vitesse, la robustesse et la performance de Vigie, ainsi que le plan de rendu. Pour le compiler : `pdflatex rapport.tex`, deux fois, dans `docs/rapport/`.

## Modèles locaux

- Vectorisation : `bge-m3`, créé par BAAI sous licence MIT, dans [une conversion GGUF publique](https://huggingface.co/lm-kit/bge-m3-gguf).
- Langage : `Qwen3.5-4B`, créé par Alibaba Cloud sous licence Apache 2.0, dans [une conversion GGUF publique](https://huggingface.co/unsloth/Qwen3.5-4B-GGUF).
- Reclassement : `bge-reranker-v2-m3`, créé par BAAI sous licence Apache 2.0, dans [une conversion GGUF publique](https://huggingface.co/gpustack/bge-reranker-v2-m3-GGUF).

Le script `scripts/download_model.ps1` télécharge des révisions précises et vérifie leurs empreintes SHA-256. Si un modèle manque, le serveur démarre avec la méthode de recherche encore disponible.
