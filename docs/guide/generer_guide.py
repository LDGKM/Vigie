"""Génère guide_code.tex à partir de guide_code.src.tex.

Chaque ligne \\extrait[légende]{fichier}{début}{fin} devient un \\lstinputlisting limité aux lignes
du fichier réel : de la première ligne qui contient « début » jusqu'à la ligne qui contient « fin »
(ou, si « fin » est vide, jusqu'à la première accolade fermante en début de ligne).
Les numéros de ligne restent donc justes après une modification du code : il suffit de relancer ce script.

La marque %%ANNEXE%% est remplacée par le code complet de chaque fichier source.

LaTeX lit des copies des fichiers placées dans docs/guide/code/ : les espaces insécables y sont
remplacées par des espaces simples, que le paquet listings ne sait pas afficher. Les numéros de ligne
sont inchangés.

Usage, depuis docs/guide : python generer_guide.py
"""
import pathlib
import re
import subprocess
import sys

ICI = pathlib.Path(__file__).resolve().parent
RACINE = ICI.parent.parent
EXTRAIT = re.compile(r"^\\extrait\[(.*?)\]\{(.*?)\}\{(.*?)\}\{(.*)\}\s*$")

ANNEXE = [
    "CMakeLists.txt", "CMakePresets.json",
    "src/main.cpp", "src/config.hpp", "src/config.cpp",
    "src/api/routes.hpp", "src/api/routes.cpp",
    "src/core/assistant.hpp", "src/core/assistant.cpp",
    "src/core/grounding.hpp", "src/core/grounding.cpp",
    "src/core/followup.hpp", "src/core/followup.cpp",
    "src/core/smalltalk.hpp", "src/core/smalltalk.cpp",
    "src/core/tool_router.hpp", "src/core/tool_router.cpp",
    "src/mcp/client.hpp", "src/mcp/client.cpp",
    "src/plan/planner.hpp", "src/plan/planner.cpp",
    "src/llm/generator.hpp", "src/llm/local_generator.hpp", "src/llm/local_generator.cpp",
    "src/corpus/types.hpp", "src/corpus/manifest.hpp", "src/corpus/manifest.cpp",
    "src/corpus/extract_html.hpp", "src/corpus/extract_html.cpp",
    "src/corpus/extract_pdf.hpp", "src/corpus/extract_pdf.cpp",
    "src/corpus/chunker.hpp", "src/corpus/chunker.cpp",
    "src/corpus/sqlite.hpp", "src/corpus/index_store.hpp", "src/corpus/index_store.cpp",
    "src/corpus/embedder.hpp", "src/corpus/embedder.cpp",
    "src/corpus/reranker.hpp", "src/corpus/reranker.cpp",
    "src/corpus/search.hpp", "src/corpus/search.cpp",
    "src/tools/console.hpp", "src/tools/vigie_index.cpp", "src/tools/vigie_search.cpp", "src/tools/vigie_eval.cpp",
    "tests/corpus_tests.cpp",
    "web/index.html",
    "scripts/download_model.ps1", "scripts/download_corpus.ps1",
]


COPIES = ICI / "code"


def copier(chemin: str) -> str:
    """Copie un fichier dans code/ sans espaces insécables et renvoie le chemin lu par LaTeX."""
    texte = (RACINE / chemin).read_text(encoding="utf-8").replace("\u00a0", " ").replace("\u202f", " ")
    cible = COPIES / chemin
    cible.parent.mkdir(parents=True, exist_ok=True)
    cible.write_text(texte, encoding="utf-8")
    return "code/" + chemin


def langage(chemin: str) -> str:
    if chemin.endswith((".cpp", ".hpp")):
        return "C++"
    if chemin.endswith(".html"):
        return "JavaScript"
    if chemin.endswith("CMakeLists.txt"):
        return "CMake"
    return "{}"


def bornes(chemin: str, debut: str, fin: str) -> tuple[int, int]:
    lignes = (RACINE / chemin).read_text(encoding="utf-8").splitlines()
    premiere = next((i for i, ligne in enumerate(lignes) if debut in ligne), None)
    if premiere is None:
        sys.exit(f"Début introuvable dans {chemin} : {debut}")
    for i in range(premiere, len(lignes)):
        if (fin and fin in lignes[i] and (i > premiere or fin not in debut)) or (not fin and lignes[i].startswith("}")):
            return premiere + 1, i + 1
    sys.exit(f"Fin introuvable dans {chemin} : {fin}")


def remplacer_extrait(ligne: str) -> str:
    trouve = EXTRAIT.match(ligne)
    if not trouve:
        return ligne
    legende, chemin, debut, fin = trouve.groups()
    a, b = bornes(chemin, debut, fin)
    titre = f"{legende} --- \\fichier{{{chemin.replace('_', chr(92) + '_')}}}, lignes {a} à {b}"
    return (f"\\lstinputlisting[language={langage(chemin)}, firstline={a}, lastline={b}, firstnumber={a}, "
            f"title={{{titre}}}]{{{copier(chemin)}}}\n")


def annexe() -> str:
    suivis = set(subprocess.check_output(["git", "ls-files"], cwd=RACINE, text=True).split())
    morceaux = []
    for chemin in ANNEXE:
        if chemin not in suivis:
            sys.exit(f"Fichier absent du dépôt : {chemin}")
        nom = chemin.replace("_", "\\_")
        morceaux.append(f"\\section*{{\\texttt{{{nom}}}}}\n\\addcontentsline{{toc}}{{section}}{{\\texttt{{{nom}}}}}\n"
                        f"\\lstinputlisting[language={langage(chemin)}, basicstyle=\\ttfamily\\scriptsize]{{{copier(chemin)}}}\n")
    oublies = sorted(f for f in suivis if f.startswith(("src/", "tests/")) and f not in ANNEXE)
    if oublies:
        sys.exit("Fichiers source absents de l'annexe : " + ", ".join(oublies))
    return "\n".join(morceaux)


def main() -> None:
    source = (ICI / "guide_code.src.tex").read_text(encoding="utf-8")
    sortie = "".join(remplacer_extrait(ligne) for ligne in source.splitlines(keepends=True))
    sortie = sortie.replace("%%ANNEXE%%", annexe())
    (ICI / "guide_code.tex").write_text(sortie, encoding="utf-8")
    print("guide_code.tex généré")


if __name__ == "__main__":
    main()
