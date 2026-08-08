#include "installer/file_hash.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace mcfix::installer {
namespace {

std::string to_hex(const std::array<unsigned char, 32>& digest) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

std::string hash_chunks(const std::vector<std::string>& chunks) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }
    for (const auto& chunk : chunks) {
        if (!chunk.empty() && BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(const_cast<char*>(chunk.data())),
                static_cast<ULONG>(chunk.size()), 0) < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptHashData failed");
        }
    }
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptFinishHash failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return to_hex(digest);
}

}  // namespace

std::string sha256_hex(std::string_view bytes) {
    return hash_chunks({std::string(bytes)});
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not open file for hashing");
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }

    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        const auto count = stream.gcount();
        if (count > 0 && BCryptHashData(
                hash, reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count), 0) < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptHashData failed");
        }
    }
    if (!stream.eof()) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("file read failed while hashing");
    }

    std::array<unsigned char, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptFinishHash failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return to_hex(digest);
}

}  // namespace mcfix::installer
