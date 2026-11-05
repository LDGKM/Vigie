cmake_minimum_required(VERSION 3.21)
project(Vigie VERSION 0.4.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Vigie se compile en 64 bits : ouvre un environnement Visual Studio x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Les dépendances sont téléchargées une seule fois, à la première configuration.
include(FetchContent)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-38-2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)
# Le dossier asio/include est un lien symbolique, absent sous Windows : on vise le vrai dossier.
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/include" CACHE PATH "Headers Asio" FORCE)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.3.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Crow)

# SQLite stocke le corpus et sa recherche plein texte (FTS5).
FetchContent_Declare(
    sqlite
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite)

add_library(vigie_sqlite STATIC "${sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(vigie_sqlite SYSTEM PUBLIC "${sqlite_SOURCE_DIR}")
target_compile_definitions(vigie_sqlite PRIVATE SQLITE_ENABLE_FTS5 SQLITE_OMIT_LOAD_EXTENSION)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(vigie_sqlite PUBLIC Threads::Threads m)
endif()

# nlohmann/json lit les fiches de data/corpus/sources.json.
FetchContent_Declare(
    nlohmann_json_header
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
    URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_header)

# PDFium lit le texte des PDF, page par page.
set(VIGIE_PDFIUM_BASE https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8125)
if(WIN32)
    set(VIGIE_PDFIUM_URL ${VIGIE_PDFIUM_BASE}/pdfium-win-x64.tgz)
    set(VIGIE_PDFIUM_HASH 9a964261760cff52570aabf038ab346d3a5034dd8b30d0d5fe8861235d356b8b)
else()
    set(VIGIE_PDFIUM_URL ${VIGIE_PDFIUM_BASE}/pdfium-linux-x64.tgz)
    set(VIGIE_PDFIUM_HASH e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659)
endif()
FetchContent_Declare(
    pdfium_binaries
    URL ${VIGIE_PDFIUM_URL}
    URL_HASH SHA256=${VIGIE_PDFIUM_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pdfium_binaries)

add_library(vigie_pdfium SHARED IMPORTED)
target_include_directories(vigie_pdfium SYSTEM INTERFACE "${pdfium_binaries_SOURCE_DIR}/include")
if(WIN32)
    set_target_properties(vigie_pdfium PROPERTIES
        IMPORTED_LOCATION "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
        IMPORTED_IMPLIB "${pdfium_binaries_SOURCE_DIR}/lib/pdfium.dll.lib"
    )
else()
    set_target_properties(vigie_pdfium PROPERTIES
        IMPORTED_LOCATION "${pdfium_binaries_SOURCE_DIR}/lib/libpdfium.so"
    )
endif()

# llama.cpp calcule localement les vecteurs de sens des passages et des questions.
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    llama_cpp
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG b12000
    GIT_SHALLOW TRUE
)
# llama.cpp se compile en C++17 : sous Visual Studio, le C++20 casse ses chaînes u8"...".
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(llama_cpp)
set(CMAKE_CXX_STANDARD 20)

function(vigie_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /bigobj)
        target_compile_definitions(${target} PRIVATE
            _WIN32_WINNT=0x0A00
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _CRT_SECURE_NO_WARNINGS
        )
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Lecture, découpage et stockage du corpus.
add_library(vigie_corpus STATIC
    src/corpus/chunker.cpp
    src/corpus/embedder.cpp
    src/corpus/extract_html.cpp
    src/corpus/extract_pdf.cpp
    src/corpus/index_store.cpp
    src/corpus/manifest.cpp
    src/corpus/search.cpp
)
target_include_directories(vigie_corpus PUBLIC src)
target_include_directories(vigie_corpus SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
target_link_libraries(vigie_corpus PUBLIC vigie_sqlite vigie_pdfium llama)
vigie_compile_options(vigie_corpus)

function(vigie_copy_pdfium target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:vigie_pdfium>" "$<TARGET_FILE_DIR:${target}>"
        )
    endif()
endfunction()

add_executable(vigie_index src/tools/vigie_index.cpp)
target_link_libraries(vigie_index PRIVATE vigie_corpus)
target_compile_definitions(vigie_index PRIVATE VIGIE_DEFAULT_ROOT="${CMAKE_SOURCE_DIR}")
vigie_compile_options(vigie_index)
vigie_copy_pdfium(vigie_index)

add_executable(vigie_search src/tools/vigie_search.cpp)
target_link_libraries(vigie_search PRIVATE vigie_corpus)
vigie_compile_options(vigie_search)
vigie_copy_pdfium(vigie_search)

enable_testing()
add_executable(vigie_tests tests/corpus_tests.cpp)
target_link_libraries(vigie_tests PRIVATE vigie_corpus)
vigie_compile_options(vigie_tests)
vigie_copy_pdfium(vigie_tests)
add_test(NAME vigie_tests COMMAND vigie_tests)

add_executable(vigie
    src/main.cpp
    src/config.cpp
    src/api/routes.cpp
    src/core/assistant.cpp
)

target_include_directories(vigie PRIVATE src)
target_link_libraries(vigie PRIVATE Crow::Crow vigie_corpus)
target_compile_definitions(vigie PRIVATE
    VIGIE_VERSION="${PROJECT_VERSION}"
    VIGIE_DEFAULT_WEB_DIR="${CMAKE_SOURCE_DIR}/web"
    VIGIE_DEFAULT_ROOT="${CMAKE_SOURCE_DIR}"
)

vigie_compile_options(vigie)
vigie_copy_pdfium(vigie)
