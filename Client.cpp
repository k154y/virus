#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cstring>

// Winsock2 must come before Windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h> 
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")

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
            exit(EXIT_FAILURE);
        }
        
        sock = socket(AF_INET, SOCK_STREAM, 0);
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        
        if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
            closesocket(sock);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            closesocket(sock);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        std::cout << "Connected to server." << std::endl;
    }

    void sendString(const std::string& str) {
        send(sock, str.c_str(), (int)str.length(), 0);
        Sleep(20); // Prevents buffer congestion
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
        if (!file.is_open()) return;

        file.seekg(0, std::ios::end);
        size_t fileSize = (size_t)file.tellg();
        file.seekg(0, std::ios::beg);

        size_t lastSlash = filepath.find_last_of("\\/");
        std::string filename = (lastSlash == std::string::npos) ? filepath : filepath.substr(lastSlash + 1);

        sendString("FILE");
        sendString(filename);
        sendString(std::to_string(fileSize));

        if (receiveString() != "READY") return;

        char buffer[BUFFER_SIZE];
        while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
            send(sock, buffer, (int)file.gcount(), 0);
        }
        file.close();
        receiveString(); // Confirmation
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
    void start(const std::string& serverIP = "127.0.0.1") {
        connectToServer(serverIP);

        char exePathBuffer[MAX_PATH];
        GetModuleFileName(NULL, exePathBuffer, MAX_PATH);
        std::string fullExePath(exePathBuffer);
        
        // Extract the directory and the name of the exe itself
        std::string currentDir = fullExePath.substr(0, fullExePath.find_last_of("\\/"));
        std::string myName = fullExePath.substr(fullExePath.find_last_of("\\/") + 1);

        std::string searchPattern = currentDir + "\\*";
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPattern.c_str(), &findData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::string itemName = findData.cFileName;
                
                // Skip self, parent, and current dir pointers
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
        std::cout << "Done." << std::endl;
    }
};

int main(int argc, char* argv[]) {
    FileTransferClient client;
    std::string ip = (argc > 1) ? argv[1] : "127.0.0.1";
    client.start(ip);
    return 0;
}