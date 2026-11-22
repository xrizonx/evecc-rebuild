#include "bluecrypto.h"
#include "util.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/objects.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <zlib.h>
#include <loguru/loguru.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace {
constexpr size_t RSA_BYTES = 128;
constexpr size_t RSA_PUBLIC_BLOB = 20 + RSA_BYTES;
constexpr size_t RSA_PRIVATE_BLOB = 20 + RSA_BYTES * 2 + (RSA_BYTES / 2) * 5;
constexpr size_t TRIPLE_DES_BYTES = 24;
constexpr uint32_t CALG_RSA_SIGN = 0x00002400;
constexpr uint32_t CALG_3DES = 0x00006603;
constexpr uint32_t CALG_RSA_KEYX = 0x0000a400;

using RsaHandle = std::unique_ptr<RSA, decltype(&RSA_free)>;
using BnHandle = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using EvpHandle = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

struct TripleDesKey { std::array<unsigned char, TRIPLE_DES_BYTES> bytes{}; };

uint32_t read_u32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_u32(unsigned char* data, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(value >> (i * 8));
}

bool write_le(const BIGNUM* bn, unsigned char* out, size_t len) {
    return bn && len <= INT_MAX &&
           BN_bn2lebinpad(bn, out, static_cast<int>(len)) == static_cast<int>(len);
}

BIGNUM* read_le(const unsigned char* data, size_t len) {
    if (len > INT_MAX) return nullptr;
    return BN_lebin2bn(data, static_cast<int>(len), nullptr);
}

std::string ssl_error() {
    const unsigned long code = ERR_get_error();
    if (!code) return "unknown OpenSSL error";
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

std::vector<unsigned char> read_file(const std::string& filename) {
    FILE* file = std::fopen(filename.c_str(), "rb");
    if (!file) return {};
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return {}; }
    const long size = std::ftell(file);
    // Key files are at most hundreds of bytes, not unbounded user data.
    if (size <= 0 || size > 1024 * 1024 ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file); return {};
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    const size_t count = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (count != bytes.size()) return {};
    return bytes;
}

bool write_file(const std::string& filename, const char* data, size_t len, bool secret) {
    if (!data || !len) return false;
    // O_EXCL prevents accidental private-key replacement and symlink attacks.
    // Enforce mode 0600 *when creating* secrets, not after writing their bytes.
    const int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW;
    const int fd = ::open(filename.c_str(), flags, secret ? 0600 : 0644);
    if (fd < 0) {
        LOG_F(ERROR, "Cannot create %s (already exists or permission denied): %s",
              filename.c_str(), get_last_error_string().c_str());
        return false;
    }
    FILE* file = ::fdopen(fd, "wb");
    if (!file) {
        ::close(fd);
        ::unlink(filename.c_str());
        return false;
    }
    const size_t written = std::fwrite(data, 1, len, file);
    const int closed = std::fclose(file);
    if (written != len || closed != 0) {
        ::unlink(filename.c_str());
        return false;
    }
    return true;
}

// CryptoAPI CryptDeriveKey(CALG_RC4, SHA1(password)) with 128-bit RC4
// (MS Enhanced provider) encrypts PRIVATEKEYBLOB secret material.
void rc4_password(unsigned char* data, size_t size, const char* password) {
    if (!password || !*password) return;
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(password),
         std::strlen(password), digest);
    std::array<unsigned char, 256> state{};
    for (int i = 0; i < 256; ++i) state[i] = static_cast<unsigned char>(i);
    unsigned int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + state[i] + digest[i % 16]) & 255U;
        std::swap(state[i], state[j]);
    }
    unsigned int i = 0;
    j = 0;
    for (size_t n = 0; n < size; ++n) {
        i = (i + 1) & 255U;
        j = (j + state[i]) & 255U;
        std::swap(state[i], state[j]);
        data[n] ^= state[(state[i] + state[j]) & 255U];
    }
    OPENSSL_cleanse(digest, sizeof(digest));
    OPENSSL_cleanse(state.data(), state.size());
}

RsaHandle load_private_key(const char* buffer, size_t length, const char* password) {
    RsaHandle rsa(RSA_new(), RSA_free);
    if (!rsa || !buffer || length != RSA_PRIVATE_BLOB) return {nullptr, RSA_free};
    std::vector<unsigned char> raw(buffer, buffer + length);
    const auto* p = raw.data();
    if (p[0] != 0x07 || p[1] != 0x02 || read_u32(p + 4) != CALG_RSA_SIGN)
        return {nullptr, RSA_free};
    // Microsoft CryptoAPI encrypts EVERYTHING after PUBLICKEYSTRUC (8 bytes),
    // including RSAPUBKEY, the modulus and the private key components.
    // Never inspect RSA2/bitlen until after decrypting that segment.
    if (password && password[0]) {
        rc4_password(raw.data() + 8, raw.size() - 8, password);
    }
    p = raw.data();
    if (read_u32(p + 8) != 0x32415352 || read_u32(p + 12) != 1024) {
        OPENSSL_cleanse(raw.data(), raw.size());
        return {nullptr, RSA_free};
    }
    size_t pos = 20;
    auto take = [&](size_t count) {
        BIGNUM* bn = read_le(p + pos, count);
        pos += count;
        return bn;
    };
    BIGNUM* n = take(RSA_BYTES);
    BIGNUM* e = BN_new();
    if (e) BN_set_word(e, read_u32(p + 16));
    BIGNUM* d = nullptr;
    BIGNUM* prime1 = take(RSA_BYTES / 2);
    BIGNUM* prime2 = take(RSA_BYTES / 2);
    BIGNUM* exponent1 = take(RSA_BYTES / 2);
    BIGNUM* exponent2 = take(RSA_BYTES / 2);
    BIGNUM* coefficient = take(RSA_BYTES / 2);
    d = take(RSA_BYTES);
    const bool ok = n && e && d && prime1 && prime2 &&
        exponent1 && exponent2 && coefficient &&
        RSA_set0_key(rsa.get(), n, e, d) == 1 &&
        RSA_set0_factors(rsa.get(), prime1, prime2) == 1 &&
        RSA_set0_crt_params(rsa.get(), exponent1, exponent2, coefficient) == 1 &&
        RSA_check_key(rsa.get()) == 1;
    OPENSSL_cleanse(raw.data(), raw.size());
    if (!ok) {
        LOG_F(ERROR, "Cannot import CryptoAPI signing key (invalid password or blob): %s",
              ssl_error().c_str());
        return {nullptr, RSA_free};
    }
    return rsa;
}

std::vector<unsigned char> export_private_key(RSA* rsa, const char* password) {
    std::vector<unsigned char> result(RSA_PRIVATE_BLOB);
    result[0] = 7; result[1] = 2;
    write_u32(result.data() + 4, CALG_RSA_SIGN);
    write_u32(result.data() + 8, 0x32415352); // RSA2
    write_u32(result.data() + 12, 1024);
    const BIGNUM *n = nullptr, *e = nullptr, *d = nullptr;
    const BIGNUM *p = nullptr, *q = nullptr;
    const BIGNUM *dmp1 = nullptr, *dmq1 = nullptr, *iqmp = nullptr;
    RSA_get0_key(rsa, &n, &e, &d);
    RSA_get0_factors(rsa, &p, &q);
    RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);
    if (!e || BN_num_bits(e) > 32) return {};
    write_u32(result.data() + 16, static_cast<uint32_t>(BN_get_word(e)));
    size_t offset = 20;
    auto append = [&](const BIGNUM* value, size_t count) {
        const bool ok = write_le(value, result.data() + offset, count);
        offset += count;
        return ok;
    };
    const bool ok = append(n, RSA_BYTES) &&
        append(p, RSA_BYTES / 2) && append(q, RSA_BYTES / 2) &&
        append(dmp1, RSA_BYTES / 2) && append(dmq1, RSA_BYTES / 2) &&
        append(iqmp, RSA_BYTES / 2) && append(d, RSA_BYTES);
    if (!ok) return {};
    // PRIVATEKEYBLOB wraps bytes starting at offset 8 (not 20).
    if (password && password[0])
        rc4_password(result.data() + 8, result.size() - 8, password);
    return result;
}

std::vector<unsigned char> export_public_key(RSA* rsa) {
    std::vector<unsigned char> result(RSA_PUBLIC_BLOB);
    result[0] = 6; result[1] = 2;
    write_u32(result.data() + 4, CALG_RSA_SIGN);
    write_u32(result.data() + 8, 0x31415352); // RSA1
    write_u32(result.data() + 12, 1024);
    const BIGNUM* n = nullptr;
    const BIGNUM* e = nullptr;
    RSA_get0_key(rsa, &n, &e, nullptr);
    if (!e || BN_num_bits(e) > 32) return {};
    write_u32(result.data() + 16, static_cast<uint32_t>(BN_get_word(e)));
    if (!write_le(n, result.data() + 20, RSA_BYTES)) return {};
    return result;
}

// SIMPLEBLOB created with an exponent-of-one RSA key. PKCS#1 type 2
// padding is still present, but exponent 1 leaves its payload unchanged.
bool unwrap_cryptoapi_3des(const char* data, size_t size, TripleDesKey& key) {
    if (!data || size != 12 + RSA_BYTES) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(data);
    if (p[0] != 1 || p[1] != 2 ||
        read_u32(p + 4) != CALG_3DES || read_u32(p + 8) != CALG_RSA_KEYX)
        return false;
    std::array<unsigned char, RSA_BYTES> block{};
    std::reverse_copy(p + 12, p + 12 + RSA_BYTES, block.begin());
    if (block[0] != 0 || block[1] != 2) return false;
    size_t end_padding = 2;
    while (end_padding < block.size() && block[end_padding] != 0)
        ++end_padding;
    if (end_padding < 10 || end_padding + 1 + TRIPLE_DES_BYTES != block.size())
        return false;
    std::copy(block.begin() + end_padding + 1, block.end(), key.bytes.begin());
    return true;
}

TripleDesKey* selected_key(CryptKeyType type) {
    if (!ctx) return nullptr;
    return static_cast<TripleDesKey*>(ctx->GetKey(type));
}

bool cipher(const unsigned char* input, size_t size, const TripleDesKey& key,
            bool encrypt, std::vector<unsigned char>& output) {
    if (!input || size > INT_MAX) return false;
    EvpHandle context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) return false;
    unsigned char iv[8] = {0};
    int n1 = 0, n2 = 0;
    output.resize(size + EVP_MAX_BLOCK_LENGTH);
    const EVP_CIPHER* algorithm = EVP_des_ede3_cbc();
    const bool ok =
        (encrypt ? EVP_EncryptInit_ex(context.get(), algorithm, nullptr,
                                      key.bytes.data(), iv)
                 : EVP_DecryptInit_ex(context.get(), algorithm, nullptr,
                                      key.bytes.data(), iv)) == 1 &&
        (encrypt ? EVP_EncryptUpdate(context.get(), output.data(), &n1,
                                    input, static_cast<int>(size))
                 : EVP_DecryptUpdate(context.get(), output.data(), &n1,
                                    input, static_cast<int>(size))) == 1 &&
        (encrypt ? EVP_EncryptFinal_ex(context.get(), output.data() + n1, &n2)
                 : EVP_DecryptFinal_ex(context.get(), output.data() + n1, &n2)) == 1;
    if (!ok) {
        LOG_F(ERROR, "3DES %s failed: %s",
              encrypt ? "encryption" : "decryption", ssl_error().c_str());
        output.clear(); return false;
    }
    output.resize(static_cast<size_t>(n1 + n2));
    return true;
}
} // namespace

CryptContext* ctx = nullptr;

HCRYPTKEY CryptContext::GetKey(CryptKeyType type) {
    // Legacy 'roaming' selects the CCP session key for code encryption.
    return type == CRYPTKEY_CCP ? (ccp_key ? ccp_key : ccp_crypt_key)
                                : ccp_crypt_key;
}

keys_blob::keys_blob() : pub_key(nullptr), priv_key(nullptr), crypt_blob(nullptr),
    pub_key_size(0), priv_key_size(0), crypt_blob_size(0) {}

bool keys_blob::dump(const char* prefix) {
    const std::string base = prefix ? prefix : "";
    bool ok = true;
    if (pub_key)
        ok = write_file(base + "pub", pub_key, pub_key_size, false) && ok;
    if (priv_key)
        ok = write_file(base + "priv", priv_key, priv_key_size, true) && ok;
    if (crypt_blob)
        ok = write_file(base + "crypt", crypt_blob, crypt_blob_size, true) && ok;
    return ok;
}

keys_blob* load_keys_blob(const char* prefix) {
    auto* result = new keys_blob();
    const std::string base = prefix ? prefix : "";
    auto copy = [](const std::string& name, char*& output, size_t& length) {
        auto bytes = read_file(name);
        if (bytes.empty()) return;
        length = bytes.size();
        output = static_cast<char*>(std::malloc(length));
        if (output) std::memcpy(output, bytes.data(), length);
        else length = 0;
    };
    copy(base + "pub", result->pub_key, result->pub_key_size);
    copy(base + "priv", result->priv_key, result->priv_key_size);
    copy(base + "crypt", result->crypt_blob, result->crypt_blob_size);
    return result;
}

int init_cryptcontext_gen(char* password) {
    if (!ctx) ctx = new CryptContext();
    ctx->password = password;
    return 0;
}

bool import_keys(char* password) {
    (void) password;
    if (!ctx) return false;
    ctx->ccp_keys = load_keys_blob(CCP_KEYS);
    TripleDesKey des;
    if (!unwrap_cryptoapi_3des(ctx->ccp_keys->crypt_blob,
                               ctx->ccp_keys->crypt_blob_size, des)) {
        LOG_F(ERROR, "Invalid ccp.keys.crypt SIMPLEBLOB; dump keys from the matching blue.dll");
        return false;
    }
    ctx->ccp_crypt_key = new TripleDesKey(des);
    ctx->ccp_key = ctx->ccp_crypt_key;
    ctx->roaming_keys = load_keys_blob(EVECC_ROAMING_KEYS);
    if (ctx->roaming_keys->pub_key_size != RSA_PUBLIC_BLOB ||
        ctx->roaming_keys->priv_key_size != RSA_PRIVATE_BLOB) {
        LOG_F(ERROR, "Missing or invalid evecc.keys.pub / evecc.keys.priv; run genkeys");
        return false;
    }
    LOG_F(INFO, "Loaded native 3DES session key and RSA signing key blobs");
    return true;
}

int init_cryptcontext(char* password) {
    if (init_cryptcontext_gen(password) != 0) return -1;
    return import_keys(password) ? 0 : -7;
}

char* SignData(char* data, uint32_t data_size, uint32_t* out_size, char* password) {
    if (out_size) *out_size = 0;
    if (!ctx || !ctx->roaming_keys || !out_size || !data) return nullptr;
    auto rsa = load_private_key(ctx->roaming_keys->priv_key,
                                ctx->roaming_keys->priv_key_size, password);
    if (!rsa) return nullptr;
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data), data_size, digest);
    std::vector<unsigned char> signature(static_cast<size_t>(RSA_size(rsa.get())));
    unsigned int length = 0;
    if (RSA_sign(NID_sha1, digest, sizeof(digest), signature.data(), &length,
                 rsa.get()) != 1) {
        LOG_F(ERROR, "RSA SHA-1 signature failed: %s", ssl_error().c_str());
        return nullptr;
    }
    // CryptSignHash returns CryptoAPI's little-endian RSA signature bytes.
    auto* output = static_cast<char*>(std::malloc(length));
    if (!output) return nullptr;
    std::reverse_copy(signature.begin(), signature.begin() + length, output);
    *out_size = length;
    return output;
}

char* JumbleString(const char* input, uint32_t input_size, uint32_t* out_size,
                   CryptKeyType type, bool zip) {
    if (out_size) *out_size = 0;
    const auto* key = selected_key(type);
    if (!key || !out_size || (!input && input_size)) return nullptr;
    std::vector<unsigned char> plain;
    if (zip) {
        uLongf size = compressBound(input_size);
        plain.resize(size);
        const int status = compress2(plain.data(), &size,
            reinterpret_cast<const Bytef*>(input), input_size, Z_DEFAULT_COMPRESSION);
        if (status != Z_OK) return nullptr;
        plain.resize(static_cast<size_t>(size));
    } else {
        plain.assign(reinterpret_cast<const unsigned char*>(input),
                     reinterpret_cast<const unsigned char*>(input) + input_size);
    }
    std::vector<unsigned char> encrypted;
    if (!cipher(plain.data(), plain.size(), *key, true, encrypted) ||
        encrypted.size() > UINT32_MAX) return nullptr;
    auto* result = static_cast<char*>(std::malloc(encrypted.size()));
    if (!result) return nullptr;
    std::memcpy(result, encrypted.data(), encrypted.size());
    *out_size = static_cast<uint32_t>(encrypted.size());
    return result;
}

uint32_t UnjumbleString(const char* input, uint32_t input_size, char* output,
                        uint32_t output_size, CryptKeyType type, bool zip) {
    const auto* key = selected_key(type);
    if (!key || !input || !output || !output_size) return 0;
    std::vector<unsigned char> decrypted;
    if (!cipher(reinterpret_cast<const unsigned char*>(input), input_size, *key,
                false, decrypted)) return 0;
    if (!zip) {
        if (decrypted.size() > output_size) return 0;
        std::memcpy(output, decrypted.data(), decrypted.size());
        return static_cast<uint32_t>(decrypted.size());
    }
    z_stream stream{};
    stream.next_in = decrypted.data();
    stream.avail_in = static_cast<uInt>(decrypted.size());
    stream.next_out = reinterpret_cast<Bytef*>(output);
    stream.avail_out = output_size;
    if (inflateInit(&stream) != Z_OK) return 0;
    const int rc = inflate(&stream, Z_FINISH);
    const uint32_t result = rc == Z_STREAM_END &&
        stream.total_out <= output_size ? static_cast<uint32_t>(stream.total_out) : 0;
    inflateEnd(&stream);
    return result;
}

bool make_code_accessors(char* password) {
    RsaHandle rsa(RSA_new(), RSA_free);
    BnHandle exponent(BN_new(), BN_free);
    if (!rsa || !exponent || BN_set_word(exponent.get(), RSA_F4) != 1 ||
        RSA_generate_key_ex(rsa.get(), 1024, exponent.get(), nullptr) != 1)
        return false;
    const auto pub = export_public_key(rsa.get());
    auto priv = export_private_key(rsa.get(), password);
    if (pub.size() != RSA_PUBLIC_BLOB || priv.size() != RSA_PRIVATE_BLOB)
        return false;
    keys_blob result;
    result.pub_key = reinterpret_cast<char*>(const_cast<unsigned char*>(pub.data()));
    result.pub_key_size = pub.size();
    result.priv_key = reinterpret_cast<char*>(priv.data());
    result.priv_key_size = priv.size();
    const bool written = result.dump(EVECC_ROAMING_KEYS);
    OPENSSL_cleanse(priv.data(), priv.size());
    return written;
}

CryptKeyType parse_key_type(char* type) {
    return type && std::strcmp(type, "ccp") == 0 ? CRYPTKEY_CCP : CRYPTKEY_ROAMING;
}
