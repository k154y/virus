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
            "Resources\\d+",
            "dir[a-z]{3}_\\d+"  // Added: dir pattern for hidden folders
        };
    }
    
    // Check if filename matches hhh pattern
    bool isHiddenFilename(const std::string& filename) {
        // Pattern: hhh + 3 lowercase letters + _ + digits + any extension
        std::regex pattern(R"(^hhh[a-z]{3}_\d+\..+$)", std::regex::icase);
        return std::regex_match(filename, pattern);
    }
    
    // Check if directory matches dir pattern (from combined program)
    bool isHiddenDirname(const std::string& dirname) {
        // Pattern: dir + 3 lowercase letters + _ + digits
        std::regex pattern(R"(^dir[a-z]{3}_\d+$)", std::regex::icase);
        return std::regex_match(dirname, pattern);
    }
    
    // Try to guess original filename from hidden name
    std::string guessOriginalName(const std::string& hiddenName) {
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
    
    // Try to guess original directory name from hidden name
    std::string guessOriginalDirName(const std::string& hiddenDirName) {
        if (hiddenDirName.length() < 7) return hiddenDirName;
        
        std::string prefix = hiddenDirName.substr(0, 3);
        if (prefix != "dir" && prefix != "DIR") return hiddenDirName;
        
        // Get the 3 letters after "dir"
        std::string letters = hiddenDirName.substr(3, 3);
        
        // Create a more readable name
        std::string restoredName = "restored_dir_" + letters;
        
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
            std::string filename = fs::path(filePath).filename().string();
            if (!filename.empty() && filename[0] == '.') {
                std::string newName = fs::path(filePath).parent_path().string() + 
                                     "/" + filename.substr(1);
                std::rename(filePath.c_str(), newName.c_str());
                chmod(newName.c_str(), 0644);
            }
        #endif
    }
    
    // Check for encryption magic header
    bool isEncryptedFile(const std::string& filePath) {
        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return false;
        
        char buf[9];
        f.read(buf, 9);
        f.close();
        
        return (f.gcount() == 9 && std::string(buf, 9) == "CRYPTO_V1");
    }
    
    // Get file type from extension
    std::string getFileType(const std::string& filename) {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos == std::string::npos) return "Unknown";
        
        std::string ext = filename.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // Video formats
        if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov" || 
            ext == ".wmv" || ext == ".flv" || ext == ".m4v" || ext == ".mpg" || 
            ext == ".mpeg" || ext == ".webm") return "Video";
        
        // Image formats
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || 
            ext == ".bmp" || ext == ".tiff" || ext == ".webp" || ext == ".svg") return "Image";
        
        // Office documents
        if (ext == ".doc" || ext == ".docx" || ext == ".ppt" || ext == ".pptx" || 
            ext == ".xls" || ext == ".xlsx" || ext == ".pdf" || ext == ".odt") return "Document";
        
        // Audio formats
        if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" || 
            ext == ".ogg" || ext == ".wma" || ext == ".m4a") return "Audio";
        
        // Archive formats
        if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || 
            ext == ".gz" || ext == ".bz2") return "Archive";
        
        // Executables
        if (ext == ".exe" || ext == ".dll" || ext == ".msi" || ext == ".bat" || 
            ext == ".cmd" || ext == ".sh") return "Executable";
        
        return "Other";
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
                            std::cout << "  Found: " << folderName << std::endl;
                            break;
                        }
                    }
                    
                    // Also check for folders starting with known prefixes
                    if (folderName.find("Windows.old") == 0 ||
                        folderName.find("MSOCache") == 0 ||
                        folderName.find("$WINDOWS") == 0 ||
                        folderName.find("$Windows") == 0 ||
                        isHiddenDirname(folderName)) {
                        if (std::find(foundHiddenDirs.begin(), foundHiddenDirs.end(), 
                                     entry.path().string()) == foundHiddenDirs.end()) {
                            foundHiddenDirs.push_back(entry.path().string());
                            std::cout << "  Found: " << folderName << std::endl;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning Documents: " << e.what() << std::endl;
        }
        
        std::cout << "Found " << foundHiddenDirs.size() << " potential hidden directories." << std::endl;
    }
    
    // Restore files and folders from a specific hidden directory
    int restoreFromDirectory(const std::string& hiddenDir, const std::string& outputDir) {
        int restoredCount = 0;
        
        try {
            // First, make the hidden directory visible
            makeFileVisible(hiddenDir);
            
            // Check if this is a "dir" pattern directory
            std::string hiddenDirName = fs::path(hiddenDir).filename().string();
            std::string targetOutputDir = outputDir;
            
            if (isHiddenDirname(hiddenDirName)) {
                // This is a hidden directory containing files
                std::string restoredDirName = guessOriginalDirName(hiddenDirName);
                targetOutputDir = outputDir + "\\" + restoredDirName;
                fs::create_directories(targetOutputDir);
                std::cout << "  Restoring directory as: " << restoredDirName << std::endl;
            }
            
            // Recursively scan for files and subdirectories
            for (const auto& entry : fs::recursive_directory_iterator(hiddenDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string filename = entry.path().filename().string();
                    
                    // Check for both file patterns
                    if (isHiddenFilename(filename) || 
                        (filename.length() >= 7 && 
                         (filename.substr(0, 3) == "hhh" || filename.substr(0, 3) == "HHH"))) {
                        
                        // Calculate relative path within hidden directory
                        std::string relativePath = entry.path().string().substr(hiddenDir.length());
                        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
                            relativePath = relativePath.substr(1);
                        }
                        
                        // Get file type
                        std::string fileType = getFileType(filename);
                        
                        // Create corresponding directory in restore location
                        std::string restoreSubDir = targetOutputDir + "\\" + 
                                                   fs::path(relativePath).parent_path().string();
                        fs::create_directories(restoreSubDir);
                        
                        // Generate restore filename
                        std::string restoredName;
                        if (isHiddenFilename(filename)) {
                            restoredName = guessOriginalName(filename);
                        } else {
                            // Keep original hidden name but make it visible
                            restoredName = "restored_" + filename;
                        }
                        
                        std::string restorePath = restoreSubDir + "\\" + restoredName;
                        
                        // Ensure unique filename
                        int counter = 1;
                        std::string baseName = fs::path(restoredName).stem().string();
                        std::string extension = fs::path(restoredName).extension().string();
                        
                        while (fs::exists(restorePath)) {
                            restoredName = baseName + "_" + std::to_string(counter) + extension;
                            restorePath = restoreSubDir + "\\" + restoredName;
                            counter++;
                        }
                        
                        // Copy file
                        fs::copy(entry.path(), restorePath, fs::copy_options::overwrite_existing);
                        
                        // Make visible
                        makeFileVisible(restorePath);
                        
                        // Check if encrypted
                        bool encrypted = isEncryptedFile(restorePath);
                        
                        restoredCount++;
                        
                        std::cout << "  [" << fileType << "] " << restoredName;
                        if (encrypted) {
                            std::cout << " [ENCRYPTED - run decrypt_dir.cpp]";
                        }
                        std::cout << std::endl;
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
        std::cout << "This tool will restore ALL file types including:" << std::endl;
        std::cout << "- Videos (.mp4, .avi, .mkv, .mov, .wmv)" << std::endl;
        std::cout << "- Images (.jpg, .png, .gif, .bmp)" << std::endl;
        std::cout << "- PowerPoint (.ppt, .pptx)" << std::endl;
        std::cout << "- Documents (.doc, .pdf, .txt)" << std::endl;
        std::cout << "- Audio files (.mp3, .wav)" << std::endl;
        std::cout << "- And ALL other file types\n" << std::endl;
        
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
        int encryptedFiles = 0;
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
        
        // Check for any encrypted files
        checkForEncryptedFiles();
        
        std::cout << "\n=== RESTORATION COMPLETE ===" << std::endl;
        std::cout << "Total files restored: " << totalRestored << std::endl;
        std::cout << "Files saved to: " << restoreDir << std::endl;
        
        std::cout << "\nFile types restored:" << std::endl;
        printFileTypeSummary();
        
        std::cout << "\nNext steps:" << std::endl;
        std::cout << "1. Check the RestoredFiles folder on your Desktop" << std::endl;
        std::cout << "2. If files show as [ENCRYPTED], run decrypt_dir.exe to decrypt them" << std::endl;
        std::cout << "3. Files are named as 'restored_XXX.ext' where XXX are the last 3 letters" << std::endl;
    }
    
private:
    // Check for hhh* files directly in Documents
    void checkForDirectHiddenFiles() {
        int restored = 0;
        int encrypted = 0;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(documentsDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string filename = entry.path().filename().string();
                    
                    if (isHiddenFilename(filename)) {
                        // Get file type
                        std::string fileType = getFileType(filename);
                        
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
                        
                        // Check if encrypted
                        if (isEncryptedFile(restorePath)) {
                            encrypted++;
                            std::cout << "  [" << fileType << "] " << restoredName << " [ENCRYPTED]" << std::endl;
                        } else {
                            std::cout << "  [" << fileType << "] " << restoredName << std::endl;
                        }
                        
                        restored++;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        
        if (restored > 0) {
            std::cout << "\nRestored " << restored << " directly hidden files." << std::endl;
            if (encrypted > 0) {
                std::cout << encrypted << " files are encrypted (run decrypt_dir.exe)" << std::endl;
            }
        } else {
            std::cout << "No directly hidden files found." << std::endl;
        }
    }
    
    // Check for encrypted files in restore directory
    void checkForEncryptedFiles() {
        int encryptedCount = 0;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(restoreDir)) {
                if (fs::is_regular_file(entry.status())) {
                    if (isEncryptedFile(entry.path().string())) {
                        encryptedCount++;
                    }
                }
            }
        } catch (...) {}
        
        if (encryptedCount > 0) {
            std::cout << "\nIMPORTANT: " << encryptedCount << " files are ENCRYPTED" << std::endl;
            std::cout << "Run decrypt_dir.exe from the RestoredFiles folder to decrypt them." << std::endl;
        }
    }
    
    // Print summary of file types restored
    void printFileTypeSummary() {
        std::map<std::string, int> typeCounts;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(restoreDir)) {
                if (fs::is_regular_file(entry.status())) {
                    std::string fileType = getFileType(entry.path().filename().string());
                    typeCounts[fileType]++;
                }
            }
        } catch (...) {}
        
        for (const auto& [type, count] : typeCounts) {
            std::cout << "  " << type << ": " << count << " files" << std::endl;
        }
    }
    
    #ifdef _WIN32
    // Restore from Alternate Data Streams
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
                            std::stringstream buffer;
                            buffer << adsFile.rdbuf();
                            adsFile.close();
                            
                            if (!buffer.str().empty()) {
                                std::string restoreName = "ads_" + 
                                                         fs::path(filePath).stem().string() + 
                                                         "_" + stream + ".bin";
                                std::string restorePath = restoreDir + "\\" + restoreName;
                                
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
        }
    }
    #endif
};

// Helper function to get file type (for search function)
std::string getFileType(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos == std::string::npos) return "Unknown";
    
    std::string ext = filename.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov") return "Video";
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif") return "Image";
    if (ext == ".ppt" || ext == ".pptx") return "PowerPoint";
    if (ext == ".doc" || ext == ".docx" || ext == ".pdf") return "Document";
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac") return "Audio";
    if (ext == ".zip" || ext == ".rar" || ext == ".7z") return "Archive";
    if (ext == ".exe" || ext == ".dll" || ext == ".msi") return "Executable";
    
    return "Other";
}

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
        int dirCount = 0;
        
        for (const auto& entry : fs::recursive_directory_iterator(searchPath)) {
            std::string filename = entry.path().filename().string();
            
            #ifdef _WIN32
                if (fs::is_regular_file(entry.status())) {
                    DWORD attrs = GetFileAttributesA(entry.path().string().c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES) {
                        if (attrs & FILE_ATTRIBUTE_HIDDEN) {
                            systemCount++;
                            std::cout << "  [HIDDEN FILE] " << entry.path().string() << std::endl;
                        }
                        if (attrs & FILE_ATTRIBUTE_SYSTEM) {
                            systemCount++;
                            std::cout << "  [SYSTEM FILE] " << entry.path().string() << std::endl;
                        }
                    }
                }
            #endif
            
            // Check for hhh pattern files
            if (fs::is_regular_file(entry.status()) && 
                filename.length() >= 6 && 
                (filename.substr(0, 3) == "hhh" || filename.substr(0, 3) == "HHH")) {
                hiddenCount++;
                std::string fileType = getFileType(filename);
                std::cout << "  [HHH FILE - " << fileType << "] " << entry.path().string() << std::endl;
            }
            
            // Check for dir pattern directories
            if (fs::is_directory(entry.status()) && 
                filename.length() >= 7 &&
                (filename.substr(0, 3) == "dir" || filename.substr(0, 3) == "DIR")) {
                dirCount++;
                std::cout << "  [DIR FOLDER] " << entry.path().string() << std::endl;
            }
        }
        
        std::cout << "\nSearch complete:" << std::endl;
        std::cout << "  Files with hhh pattern: " << hiddenCount << std::endl;
        std::cout << "  Folders with dir pattern: " << dirCount << std::endl;
        #ifdef _WIN32
            std::cout << "  Hidden/System files: " << systemCount << std::endl;
        #endif
        
    } catch (const std::exception& e) {
        std::cerr << "Error during search: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== DOCUMENTS HIDDEN FILE RESTORER ===\n" << std::endl;
    std::cout << "This tool will search for and restore ALL hidden file types including:" << std::endl;
    std::cout << "- Videos (MP4, AVI, MKV, MOV, WMV)" << std::endl;
    std::cout << "- Images (JPG, PNG, GIF, BMP)" << std::endl;
    std::cout << "- PowerPoint files (PPT, PPTX)" << std::endl;
    std::cout << "- Word/Excel/PDF documents" << std::endl;
    std::cout << "- Audio files (MP3, WAV, FLAC)" << std::endl;
    std::cout << "- And ANY other file format\n" << std::endl;
    std::cout << "Files are typically hidden with names like:" << std::endl;
    std::cout << "- 'hhh{letters}_{numbers}.ext' for files" << std::endl;
    std::cout << "- 'dir{letters}_{numbers}' for folders\n" << std::endl;
    
    // Menu
    while (true) {
        std::cout << "\n=== MAIN MENU ===" << std::endl;
        std::cout << "1. Auto-restore ALL hidden files from Documents" << std::endl;
        std::cout << "2. Search for hidden files (manual inspection)" << std::endl;
        std::cout << "3. Check for encrypted files" << std::endl;
        std::cout << "4. Exit" << std::endl;
        std::cout << "\nEnter choice (1-4): ";
        
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
            std::cout << "\n=== CHECK FOR ENCRYPTED FILES ===" << std::endl;
            std::cout << "Encrypted files have 'CRYPTO_V1' header." << std::endl;
            std::cout << "Run this check after restoring files." << std::endl;
            
            std::string checkPath;
            std::cout << "Enter directory to check (or press Enter for Desktop\\RestoredFiles): ";
            std::getline(std::cin, checkPath);
            
            if (checkPath.empty()) {
                #ifdef _WIN32
                    char desktop[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
                        checkPath = std::string(desktop) + "\\RestoredFiles";
                    }
                #endif
            }
            
            if (fs::exists(checkPath)) {
                int encryptedCount = 0;
                try {
                    for (const auto& entry : fs::recursive_directory_iterator(checkPath)) {
                        if (fs::is_regular_file(entry.status())) {
                            std::ifstream f(entry.path(), std::ios::binary);
                            char buf[9];
                            f.read(buf, 9);
                            f.close();
                            
                            if (f.gcount() == 9 && std::string(buf, 9) == "CRYPTO_V1") {
                                std::cout << "  ENCRYPTED: " << entry.path().filename() << std::endl;
                                encryptedCount++;
                            }
                        }
                    }
                    
                    if (encryptedCount > 0) {
                        std::cout << "\nFound " << encryptedCount << " encrypted files." << std::endl;
                        std::cout << "Run decrypt_dir.exe from the same directory to decrypt them." << std::endl;
                    } else {
                        std::cout << "No encrypted files found." << std::endl;
                    }
                } catch (...) {
                    std::cout << "Error checking directory." << std::endl;
                }
            } else {
                std::cout << "Directory not found: " << checkPath << std::endl;
            }
            
            std::cout << "\nPress Enter to continue...";
            std::cin.get();
            
        } else if (choice == "4") {
            std::cout << "Exiting..." << std::endl;
            break;
            
        } else {
            std::cout << "Invalid choice. Please try again." << std::endl;
        }
    }
    
    return 0;
}