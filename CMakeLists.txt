cmake_minimum_required(VERSION 3.21)
project(Vigie VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(vigie
    src/main.cpp
    src/config.cpp
    src/api/routes.cpp
    src/core/assistant.cpp
)

target_include_directories(vigie PRIVATE src)
target_link_libraries(vigie PRIVATE Crow::Crow)
target_compile_definitions(vigie PRIVATE
    VIGIE_VERSION="${PROJECT_VERSION}"
    VIGIE_DEFAULT_WEB_DIR="${CMAKE_SOURCE_DIR}/web"
)

if(MSVC)
    target_compile_options(vigie PRIVATE /W4 /utf-8 /bigobj)
    target_compile_definitions(vigie PRIVATE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
else()
    target_compile_options(vigie PRIVATE -Wall -Wextra -Wpedantic)
endif()
