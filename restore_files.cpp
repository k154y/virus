#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <regex>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <direct.h>
    #define getcwd _getcwd
    #pragma comment(lib, "shlwapi.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <fcntl.h>
#endif

namespace fs = std::filesystem;

class DocumentsRestorer {
private:
    std::string documentsDir;
    std::string restoreDir;
    std::vector<std::string> foundHiddenDirs;
    
    // Get Documents directory
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
    
    // List of Windows-like folder patterns to look for
    std::vector<std::string> getHiddenFolderPatterns() {
        return {
            "Windows\\.old\\d+",
            "MSOCache\\d+",
            "\\$WINDOWS\\.~BT\\d+",
            "Boot\\d+",
            "Documents and Settings\\d+",
            "PerfLogs\\d+",
            "ProgramData\\d+",
            "Recovery\\d+",
            "System Volume Information\\d+",
            "WindowsUpdate\\d+",
            "WinSxS\\d+",
            "SysWOW64\\d+",
            "Media\\d+",
            "Prefetch\\d+",
            "Fonts\\d+",
            "Cursors\\d+",
            "Resources\\d+"
        };
    }
    
    // Check if filename matches hhh pattern
    bool isHiddenFilename(const std::string& filename) {
        // Pattern: hhh + 3 lowercase letters + _ + digits + extension
        std::regex pattern(R"(^hhh[a-z]{3}_\d+\..+$)", std::regex::icase);
        return std::regex_match(filename, pattern);
    }
    
    // Try to guess original filename from hidden name
    std::string guessOriginalName(const std::string& hiddenName) {
        // Extract the 3 letters from "hhh{letters}_{numbers}.ext"
        if (hiddenName.length() < 7) return hiddenName;
        
        std::string prefix = hiddenName.substr(0, 3);
        if (prefix != "hhh" && prefix != "HHH") return hiddenName;
        
        // Get the 3 letters after "hhh"
        std::string letters = hiddenName.substr(3, 3);
        
        // Find extension
        size_t dotPos = hiddenName.find_last_of('.');
        if (dotPos == std::string::npos) return hiddenName;
        
        std::string extension = hiddenName.substr(dotPos);
        
        // Create a more readable name
        std::string restoredName = "restored_" + letters + extension;
        
        return restoredName;
    }
    
    // Remove hidden attributes from file
    void makeFileVisible(const std::string& filePath) {
        #ifdef _WIN32
            DWORD attrs = GetFileAttributesA(filePath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES) {
                // Remove HIDDEN and SYSTEM attributes
                attrs &= ~(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
                SetFileAttributesA(filePath.c_str(), attrs);
            }
        #else
            // On Unix/Linux, rename to remove dot prefix if present
            std::string filename = fs::path(filePath).filename().string();
            if (!filename.empty() && filename[0] == '.') {
                std::string newName = fs::path(filePath).parent_path().string() + 
                                     "/" + filename.substr(1);
                std::rename(filePath.c_str(), newName.c_str());
                chmod(newName.c_str(), 0644); // Normal permissions
            }
        #endif
    }
    
    // Scan Documents for hidden folders
    void scanForHiddenFolders() {
        std::cout << "Scanning Documents folder for hidden directories..." << std::endl;
        
        std::vector<std::string> patterns = getHiddenFolderPatterns();
        
        try {
            for (const auto& entry : fs::directory_iterator(documentsDir)) {
                if (fs::is_directory(entry.status())) {
                    std::string folderName = entry.path().filename().string();
                    
                    // Check if folder name matches any pattern
                    for (const auto& pattern : patterns) {
                        std::regex re(pattern, std::regex::icase);
                        if (std::regex_match(folderName, re)) {
                            foundHiddenDirs.push_back(entry.path().string());
                            break;
                        }
                    }
                    
                    // Also check for folders starting with known prefixes
                    if (folderName.find("Windows.old") == 0 ||
                        folderName.find("MSOCache") == 0 ||
                        folderName.find("$WINDOWS") == 0 ||
                        folderName.find("$Windows") == 0) {
                        foundHiddenDirs.push_back(entry.path().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning Documents: " << e.what() << std::endl;
        }
        
        std::cout << "Found " << foundHiddenDirs.size() << " potential hidden directories." << std::endl;
    }
    
    // Restore files from a specific hidden directory
    int restoreFromDirectory(const std::string& hiddenDir, const std::string& outputDir) {
        int restoredCount = 0;
        
        try {
            // First, make the hidden directory visible
            makeFileVisible(hiddenDir);
            
            // Recursively scan for hhh* files
            for (const auto& entry : fs::recursive_directory_iterator(hiddenDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string filename = entry.path().filename().string();
                    
                    if (isHiddenFilename(filename)) {
                        // Create restore path
                        std::string relativePath = entry.path().string().substr(hiddenDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        // Create corresponding directory in restore location
                        std::string restoreSubDir = outputDir + "\\" + 
                                                   fs::path(relativePath).parent_path().string();
                        fs::create_directories(restoreSubDir);
                        
                        // Generate restore filename
                        std::string restoredName = guessOriginalName(filename);
                        std::string restorePath = restoreSubDir + "\\" + restoredName;
                        
                        // Copy and make visible
                        fs::copy(entry.path(), restorePath, fs::copy_options::overwrite_existing);
                        makeFileVisible(restorePath);
                        
                        restoredCount++;
                        
                        std::cout << "  Restored: " << restoredName << std::endl;
                    }
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error restoring from " << hiddenDir << ": " << e.what() << std::endl;
        }
        
        return restoredCount;
    }

public:
    DocumentsRestorer() {
        // Get Documents directory
        documentsDir = getDocumentsDirectory();
        if (documentsDir.empty()) {
            std::cerr << "Error: Could not find Documents directory!" << std::endl;
            exit(1);
        }
        
        // Create restore directory on Desktop
        #ifdef _WIN32
            char desktopPath[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
                restoreDir = std::string(desktopPath) + "\\RestoredFiles";
            } else {
                char* userProfile = getenv("USERPROFILE");
                restoreDir = std::string(userProfile) + "\\Desktop\\RestoredFiles";
            }
        #else
            const char* home = getenv("HOME");
            restoreDir = std::string(home) + "/Desktop/RestoredFiles";
        #endif
        
        std::cout << "Documents directory: " << documentsDir << std::endl;
        std::cout << "Restore directory: " << restoreDir << std::endl;
    }
    
    // Scan and restore all hidden files
    void restoreAllFiles() {
        std::cout << "\n=== DOCUMENTS FILE RESTORER ===\n" << std::endl;
        
        // Create restore directory
        fs::create_directories(restoreDir);
        
        // Scan for hidden folders
        scanForHiddenFolders();
        
        if (foundHiddenDirs.empty()) {
            std::cout << "\nNo hidden directories found in Documents." << std::endl;
            std::cout << "Checking for files with hhh* pattern..." << std::endl;
            
            // Check for hhh* files directly in Documents
            checkForDirectHiddenFiles();
            return;
        }
        
        std::cout << "\nFound hidden directories:" << std::endl;
        for (size_t i = 0; i < foundHiddenDirs.size(); i++) {
            std::cout << "  [" << i + 1 << "] " << foundHiddenDirs[i] << std::endl;
        }
        
        // Restore from each directory
        int totalRestored = 0;
        for (const auto& hiddenDir : foundHiddenDirs) {
            std::cout << "\nRestoring from: " << hiddenDir << std::endl;
            int count = restoreFromDirectory(hiddenDir, restoreDir);
            totalRestored += count;
            std::cout << "  Restored " << count << " files from this directory." << std::endl;
        }
        
        // Also check for Alternate Data Streams (if on Windows)
        #ifdef _WIN32
            restoreFromADS();
        #endif
        
        std::cout << "\n=== RESTORATION COMPLETE ===" << std::endl;
        std::cout << "Total files restored: " << totalRestored << std::endl;
        std::cout << "Files saved to: " << restoreDir << std::endl;
        
        if (totalRestored > 0) {
            std::cout << "\nNote: Original filenames may not be perfectly restored." << std::endl;
            std::cout << "Files are named as 'restored_XXX.extension' where XXX are" << std::endl;
            std::cout << "the last 3 letters from the original filename." << std::endl;
        }
    }
    
private:
    // Check for hhh* files directly in Documents (not in subfolders)
    void checkForDirectHiddenFiles() {
        int restored = 0;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(documentsDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string filename = entry.path().filename().string();
                    
                    if (isHiddenFilename(filename)) {
                        // Make file visible first
                        makeFileVisible(entry.path().string());
                        
                        // Create restore path
                        std::string restoredName = guessOriginalName(filename);
                        std::string restorePath = restoreDir + "\\" + restoredName;
                        
                        // Ensure unique filename
                        int counter = 1;
                        while (fs::exists(restorePath)) {
                            std::string newName = fs::path(restoredName).stem().string() + 
                                                 "_" + std::to_string(counter) + 
                                                 fs::path(restoredName).extension().string();
                            restorePath = restoreDir + "\\" + newName;
                            counter++;
                        }
                        
                        // Copy file
                        fs::copy(entry.path(), restorePath, fs::copy_options::overwrite_existing);
                        restored++;
                        
                        std::cout << "  Restored: " << restoredName << std::endl;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        
        if (restored > 0) {
            std::cout << "\nRestored " << restored << " directly hidden files." << std::endl;
        } else {
            std::cout << "No directly hidden files found." << std::endl;
        }
    }
    
    #ifdef _WIN32
    // Restore from Alternate Data Streams (Advanced)
    void restoreFromADS() {
        std::cout << "\nChecking for Alternate Data Streams..." << std::endl;
        
        int adsRestored = 0;
        
        try {
            for (const auto& entry : fs::directory_iterator(documentsDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string filePath = entry.path().string();
                    
                    // Check for common ADS stream names
                    std::vector<std::string> streamNames = {
                        "Zone.Identifier",
                        "Zone.Identifier:0",
                        "Zone.Identifier:1",
                        "data",
                        "hidden",
                        "secret"
                    };
                    
                    for (const auto& stream : streamNames) {
                        std::string adsPath = filePath + ":" + stream;
                        std::ifstream adsFile(adsPath, std::ios::binary);
                        
                        if (adsFile.good()) {
                            // Read the stream
                            std::stringstream buffer;
                            buffer << adsFile.rdbuf();
                            adsFile.close();
                            
                            if (!buffer.str().empty()) {
                                // Create restore filename
                                std::string restoreName = "ads_" + 
                                                         fs::path(filePath).stem().string() + 
                                                         "_" + stream + ".bin";
                                std::string restorePath = restoreDir + "\\" + restoreName;
                                
                                // Save the stream content
                                std::ofstream outFile(restorePath, std::ios::binary);
                                outFile << buffer.str();
                                outFile.close();
                                
                                adsRestored++;
                                std::cout << "  Restored ADS from: " << entry.path().filename().string() 
                                          << ":" << stream << std::endl;
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error checking ADS: " << e.what() << std::endl;
        }
        
        if (adsRestored > 0) {
            std::cout << "Restored " << adsRestored << " files from Alternate Data Streams." << std::endl;
            std::cout << "Note: ADS files are saved as .bin files in the restore directory." << std::endl;
        }
    }
    #endif
};

// Function to search for hidden files with a specific pattern
void searchForHiddenFiles() {
    std::cout << "\n=== ADVANCED SEARCH ===" << std::endl;
    
    // Get user input for search
    std::string searchPath;
    std::cout << "Enter directory to search (or press Enter for Documents): ";
    std::getline(std::cin, searchPath);
    
    if (searchPath.empty()) {
        #ifdef _WIN32
            char path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path))) {
                searchPath = std::string(path);
            }
        #else
            const char* home = getenv("HOME");
            searchPath = std::string(home) + "/Documents";
        #endif
    }
    
    std::cout << "Searching in: " << searchPath << std::endl;
    
    try {
        int hiddenCount = 0;
        int systemCount = 0;
        
        for (const auto& entry : fs::recursive_directory_iterator(searchPath)) {
            if (fs::is_regular_file(entry.status())) {
                std::string filename = entry.path().filename().string();
                
                #ifdef _WIN32
                    // Check file attributes
                    DWORD attrs = GetFileAttributesA(entry.path().string().c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES) {
                        if (attrs & FILE_ATTRIBUTE_HIDDEN) {
                            systemCount++;
                            std::cout << "  [HIDDEN] " << entry.path().string() << std::endl;
                        }
                        if (attrs & FILE_ATTRIBUTE_SYSTEM) {
                            systemCount++;
                            std::cout << "  [SYSTEM] " << entry.path().string() << std::endl;
                        }
                    }
                #endif
                
                // Check for hhh pattern
                if (filename.length() >= 6 && 
                    (filename.substr(0, 3) == "hhh" || filename.substr(0, 3) == "HHH")) {
                    hiddenCount++;
                    std::cout << "  [HHH PATTERN] " << entry.path().string() << std::endl;
                }
            }
        }
        
        std::cout << "\nSearch complete:" << std::endl;
        std::cout << "  Files with hhh pattern: " << hiddenCount << std::endl;
        #ifdef _WIN32
            std::cout << "  Hidden/System files: " << systemCount << std::endl;
        #endif
        
    } catch (const std::exception& e) {
        std::cerr << "Error during search: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== DOCUMENTS HIDDEN FILE RESTORER ===\n" << std::endl;
    std::cout << "This tool will search for and restore files hidden by the stealth program." << std::endl;
    std::cout << "Files are typically hidden in Documents with names like 'hhh{letters}_{numbers}.ext'\n" << std::endl;
    
    // Menu
    while (true) {
        std::cout << "\n=== MAIN MENU ===" << std::endl;
        std::cout << "1. Auto-restore all hidden files from Documents" << std::endl;
        std::cout << "2. Search for hidden files (manual inspection)" << std::endl;
        std::cout << "3. Exit" << std::endl;
        std::cout << "\nEnter choice (1-3): ";
        
        std::string choice;
        std::getline(std::cin, choice);
        
        if (choice == "1") {
            DocumentsRestorer restorer;
            restorer.restoreAllFiles();
            
            std::cout << "\nPress Enter to continue...";
            std::cin.get();
            
        } else if (choice == "2") {
            searchForHiddenFiles();
            
            std::cout << "\nPress Enter to continue...";
            std::cin.get();
            
        } else if (choice == "3") {
            std::cout << "Exiting..." << std::endl;
            break;
            
        } else {
            std::cout << "Invalid choice. Please try again." << std::endl;
        }
    }
    
    return 0;
}