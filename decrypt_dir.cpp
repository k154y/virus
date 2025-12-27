#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

const std::string PASSWORD = "StrongPassword@123";
const std::string MAGIC = "CRYPTO_V1";
constexpr int KEY_SIZE = 32, SALT_SIZE = 16, IV_SIZE = 12, TAG_SIZE = 16, ITER = 100000;

void deriveKey(const unsigned char* salt, unsigned char* key) {
    PKCS5_PBKDF2_HMAC(PASSWORD.c_str(), PASSWORD.size(), salt, SALT_SIZE, ITER, EVP_sha256(), KEY_SIZE, key);
}

void decryptFile(const fs::path& filePath) {
    std::ifstream inFile(filePath, std::ios::binary);
    
    // Skip the Magic Header
    inFile.seekg(MAGIC.size());

    unsigned char salt[SALT_SIZE], iv[IV_SIZE], tag[TAG_SIZE];
    inFile.read((char*)salt, SALT_SIZE);
    inFile.read((char*)iv, IV_SIZE);
    inFile.read((char*)tag, TAG_SIZE);

    std::vector<unsigned char> cipher((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    unsigned char key[KEY_SIZE];
    deriveKey(salt, key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag);

    std::vector<unsigned char> plain(cipher.size());
    int len;
    int ret = EVP_DecryptUpdate(ctx, plain.data(), &len, cipher.data(), (int)cipher.size());
    
    if (ret > 0 && EVP_DecryptFinal_ex(ctx, plain.data() + len, &len) > 0) {
        EVP_CIPHER_CTX_free(ctx);
        std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
        outFile.write((char*)plain.data(), plain.size());
    } else {
        EVP_CIPHER_CTX_free(ctx);
        std::cerr << "[!] Fail: " << filePath.filename() << "\n";
    }
}

int main(int argc, char* argv[]) {
    fs::path target = fs::current_path();
    int count = 0;
    for (auto& e : fs::recursive_directory_iterator(target)) {
        if (e.is_regular_file()) {
            // Re-use the check logic
            std::ifstream f(e.path(), std::ios::binary);
            char buf[9]; f.read(buf, 9); f.close();
            if (std::string(buf, 9) == MAGIC) {
                decryptFile(e.path());
                std::cout << "[*] Restored: " << e.path().filename() << "\n";
                count++;
            }
        }
    }
    std::cout << "\nTotal Restored: " << count << "\nPress Enter...";
    std::cin.get();
    return 0;
}