cmake_minimum_required(VERSION 3.10)

project(Cryption LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

option(AES_ENABLE_INPLACE "Enable AES-GCM in-place mode" OFF)
option(AES_ENABLE_CTX_REUSE "Enable AES-GCM context reuse" OFF)
option(CHACHA20_ENABLE_INPLACE "Enable ChaCha20-Poly1305 in-place mode" OFF)

add_executable(aes_encrypt
    aes/aes.cpp
    aes/gmult.cpp
    aes/aes_gcm.cpp
    aes/aes_interface.cpp
    aes/aes_main.cpp
)

target_include_directories(aes_encrypt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/aes
)

target_compile_options(aes_encrypt PRIVATE
    -Wall
    -Wextra
    -march=armv8-a
)

if(AES_ENABLE_INPLACE)
    target_compile_definitions(aes_encrypt PRIVATE AES_ENABLE_INPLACE)
endif()

if(AES_ENABLE_CTX_REUSE)
    target_compile_definitions(aes_encrypt PRIVATE AES_ENABLE_CTX_REUSE)
endif()

add_executable(chacha20_encrypt
    chacha20/chacha20_poly1305.cpp
    chacha20/chacha20_interface.cpp
    chacha20/chacha20_main.cpp
)

target_include_directories(chacha20_encrypt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/chacha20
)

target_compile_options(chacha20_encrypt PRIVATE
    -Wall
    -Wextra
    -march=armv8-a
)

if(CHACHA20_ENABLE_INPLACE)
    target_compile_definitions(chacha20_encrypt PRIVATE CHACHA20_ENABLE_INPLACE)
endif()
