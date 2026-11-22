#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#else
// Windows CryptoAPI handles are opaque to callers. The Linux backend uses
// OpenSSL instead; these aliases keep the caller-facing interface unchanged.
using HCRYPTPROV = void*;
using HCRYPTKEY = void*;
using DWORD = uint32_t;
#endif

#define EVECC_ROAMING_KEYS "evecc.keys."
#define CCP_KEYS "ccp.keys."

enum CryptKeyType {
    CRYPTKEY_CCP,
    CRYPTKEY_ROAMING,
};

static const char* key_types[] = { "ccp", "roaming", nullptr };

struct keys_blob {
    char* pub_key;
    char* priv_key;
    char* crypt_blob;
    size_t pub_key_size;
    size_t priv_key_size;
    size_t crypt_blob_size;
    keys_blob();
    bool dump(const char* prefix);
};

struct CryptContext {
    char* password = nullptr;
    CryptKeyType set_key_type = CRYPTKEY_ROAMING;
    HCRYPTPROV context = nullptr;
    HCRYPTPROV sign_context = nullptr;
    HCRYPTKEY ccp_key = nullptr;
    HCRYPTKEY roaming_crypt_key = nullptr;
    HCRYPTKEY ccp_crypt_key = nullptr;
    HCRYPTKEY roaming_priv = nullptr;
    HCRYPTKEY roaming_pub = nullptr;
    keys_blob* roaming_keys = nullptr;
    keys_blob* ccp_keys = nullptr;
    HCRYPTKEY GetKey(CryptKeyType type);
};

extern CryptContext* ctx;

int init_cryptcontext(char* password);
int init_cryptcontext_gen(char* password);
char* SignData(char* data, uint32_t data_size, uint32_t* out_size, char* password);
uint32_t UnjumbleString(const char* input, uint32_t input_size,
                        char* output, uint32_t output_size,
                        CryptKeyType key_type, bool zip = true);
char* JumbleString(const char* input, uint32_t input_size,
                   uint32_t* read_bytes, CryptKeyType key_type, bool zip = true);
bool make_code_accessors(char* password);
bool import_keys(char* password);
keys_blob* load_keys_blob(const char* prefix);
CryptKeyType parse_key_type(char* type);

#ifdef _WIN32
// CryptoAPI-specific internals; not needed by the OpenSSL native backend.
char* smart_export_key(HCRYPTKEY key, DWORD blobtype, size_t* out_size,
                       HCRYPTKEY expKey);
HCRYPTKEY generate_key_from_password(char* password, HCRYPTPROV context);
#endif
