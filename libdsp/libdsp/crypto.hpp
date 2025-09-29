/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Encryption/decrpytion
 *
 * Initial, draft implementation by our friend, ChatGPT.
 *
 * Key Points
 * - AES-256-GCM → Authenticated encryption (AEAD) with very good speed on
 * x86/x64.
 * - Random 96-bit IV per field (recommended by NIST).
 * - Tag (16 bytes) ensures tamper detection.
 *
 * IV = Initialization Vector (IV)
 * Its purpose is to ensure freshness and uniqueness for each encryption
 * operation, even if the key and the plaintext are the same.
 *
 * TODO: Proper API and documentation.
 *
 * Reference: https://linux.die.net/man/3/evp_decryptfinal_ex
 *
 * Initial benchmark results:
 *
 * Run on (8 X 4700 MHz CPU s)
 * CPU Caches:
 *   L1 Data 48 KiB (x4)
 *   L1 Instruction 32 KiB (x4)
 *   L2 Unified 1280 KiB (x4)
 *   L3 Unified 12288 KiB (x1)
 * Load Average: 2.05, 1.86, 1.66
 * ***WARNING*** CPU scaling is enabled, the benchmark real time measurements may be noisy and will incur extra overhead.
 * ------------------------------------------------------------
 * Benchmark                  Time             CPU   Iterations
 * ------------------------------------------------------------
 * encrypt                  810 ns          808 ns     17268319
 * encrypt_n_decrypt       1322 ns         1320 ns     10647335
 */

#pragma once

#include <libnova/data.hpp>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace dsp {

struct encrypted_data {
    std::vector<uint8_t> iv;    // 12 bytes
    std::vector<uint8_t> tag;   // 16 bytes
    std::vector<uint8_t> cipher;// ciphertext
};

[[nodiscard]]
inline auto encrypt(nova::data_view data, nova::data_view key) -> encrypted_data {
    if (key.size() != 32) {
        throw std::runtime_error("Key must be 32 bytes");
    }

    encrypted_data out;
    out.iv.resize(12);
    RAND_bytes(out.iv.data(), static_cast<int>(out.iv.size()));

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(out.iv.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.ptr()), out.iv.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EncryptInit failed");
    }

    out.cipher.resize(data.size());
    int len;
    if (EVP_EncryptUpdate(ctx, out.cipher.data(), &len,
                          reinterpret_cast<const unsigned char*>(data.ptr()),
                          static_cast<int>(data.size())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EncryptUpdate failed");
    }

    int final_len;
    if (EVP_EncryptFinal_ex(ctx, out.cipher.data() + len, &final_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EncryptFinal failed");
    }
    out.cipher.resize(static_cast<std::size_t>(len + final_len));

    out.tag.resize(16);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                            static_cast<int>(out.tag.size()),
                            out.tag.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GetTag failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    return out;
}

[[nodiscard]]
inline auto decrypt(const encrypted_data& in, nova::data_view key) -> nova::bytes {
    if (key.size() != 32) {
        throw std::runtime_error("Key must be 32 bytes");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(in.iv.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.ptr()), in.iv.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("DecryptInit failed");
    }

    std::vector<uint8_t> plaintext(in.cipher.size());
    int len;
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          in.cipher.data(),
                          static_cast<int>(in.cipher.size())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("DecryptUpdate failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                            static_cast<int>(in.tag.size()),
                            const_cast<uint8_t*>(in.tag.data())) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("SetTag failed");
    }

    int final_len;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &final_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Authentication failed: bad key or corrupted data");
    }
    plaintext.resize(static_cast<std::size_t>(len + final_len));
    EVP_CIPHER_CTX_free(ctx);
    return nova::data_view{ plaintext }.to_vec();
}

} // namespace dsp
