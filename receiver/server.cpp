#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_PATH_LENGTH 4096

class FileTransferServer {
private:
    SOCKET server_fd, client_socket;
    sockaddr_in address;
    int addrlen = sizeof(address);
    WSADATA wsaData;
    
    void createSocket() {
        // Initialize Winsock
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            exit(EXIT_FAILURE);
        }
        
        // Creating socket file descriptor
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == INVALID_SOCKET) {
            std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        // Forcefully attaching socket to the port
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            std::cerr << "Setsockopt failed: " << WSAGetLastError() << std::endl;
            closesocket(server_fd);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);
        
        // Bind the socket to localhost port
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) {
            std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
            closesocket(server_fd);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
    }
    
    void listenForConnections() {
        if (listen(server_fd, 3) == SOCKET_ERROR) {
            std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
            closesocket(server_fd);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        std::cout << "Server listening on port " << PORT << std::endl;
        std::cout << "Files will be saved in: " << getCurrentDirectory() << std::endl;
    }
    
    void acceptConnection() {
        client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            closesocket(server_fd);
            WSACleanup();
            exit(EXIT_FAILURE);
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Connection accepted from " << clientIP << std::endl;
    }
    
    std::string receiveString() {
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);
        int bytesRead = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytesRead <= 0) {
            return "";
        }
        return std::string(buffer);
    }
    
    void sendString(const std::string& str) {
        send(client_socket, str.c_str(), str.length(), 0);
    }
    
    std::string getCurrentDirectory() {
        char buffer[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, buffer);
        return std::string(buffer);
    }
    
    void createDirectory(const std::string& path) {
        // Create all directories in the path
        std::string currentPath;
        std::stringstream ss(path);
        std::string segment;
        std::vector<std::string> segments;
        
        // Split by both forward and backward slashes
        while (std::getline(ss, segment, '/')) {
            std::stringstream ss2(segment);
            std::string segment2;
            while (std::getline(ss2, segment2, '\\')) {
                if (!segment2.empty()) {
                    segments.push_back(segment2);
                }
            }
        }
        
        // Build and create directories starting from current directory
        currentPath = getCurrentDirectory();
        for (const auto& seg : segments) {
            currentPath = currentPath + "\\" + seg;
            
            // Check if directory exists
            DWORD attrib = GetFileAttributes(currentPath.c_str());
            if (attrib == INVALID_FILE_ATTRIBUTES || !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
                if (_mkdir(currentPath.c_str()) != 0) {
                    // Try to create with full permissions
                    if (CreateDirectory(currentPath.c_str(), NULL) == 0) {
                        std::cerr << "Warning: Failed to create directory: " << currentPath << std::endl;
                    }
                }
            }
        }
    }
    
    void receiveFile(const std::string& filepath, size_t fileSize) {
        std::cout << "Receiving file: " << filepath << " (" << fileSize << " bytes)" << std::endl;
        
        // Create parent directories if needed
        size_t pos = filepath.find_last_of("/\\");
        if (pos != std::string::npos) {
            std::string dir = filepath.substr(0, pos);
            if (!dir.empty()) {
                createDirectory(dir);
            }
        }
        
        // Build full path in current directory
        std::string fullPath = getCurrentDirectory() + "\\" + filepath;
        
        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << fullPath << std::endl;
            return;
        }
        
        char buffer[BUFFER_SIZE];
        size_t totalReceived = 0;
        
        while (totalReceived < fileSize) {
            size_t toRead = std::min((size_t)BUFFER_SIZE, fileSize - totalReceived);
            int bytesRead = recv(client_socket, buffer, toRead, 0);
            
            if (bytesRead <= 0) {
                std::cerr << "Error receiving file data" << std::endl;
                break;
            }
            
            file.write(buffer, bytesRead);
            totalReceived += bytesRead;
            
            // Show progress
            int progress = (int)((double)totalReceived / fileSize * 100);
            std::cout << "\rProgress: " << progress << "% (" << totalReceived << "/" << fileSize << " bytes)";
            std::cout.flush();
        }
        
        file.close();
        std::cout << "\nFile saved to: " << fullPath << std::endl;
    }
    
    void handleFileTransfer() {
        while (true) {
            // Receive command type
            std::string command = receiveString();
            
            if (command.empty()) {
                std::cout << "Client disconnected" << std::endl;
                break;
            }
            
            if (command == "QUIT") {
                std::cout << "Client requested disconnect" << std::endl;
                break;
            }
            else if (command == "FILE") {
                // Receive file info
                std::string filename = receiveString();
                std::string sizeStr = receiveString();
                size_t fileSize = std::stoull(sizeStr);
                
                sendString("READY"); // Tell client we're ready to receive
                
                // Receive the file
                receiveFile(filename, fileSize);
                
                sendString("SUCCESS");
            }
            else if (command == "FOLDER") {
                std::string foldername = receiveString();
                std::cout << "Creating folder: " << foldername << std::endl;
                
                // Create folder in current directory
                createDirectory(foldername);
                sendString("READY");
                
                // Receive number of files
                std::string numFilesStr = receiveString();
                int numFiles = std::stoi(numFilesStr);
                
                sendString("READY_FILES");
                
                // Receive each file in folder
                for (int i = 0; i < numFiles; i++) {
                    std::string relativePath = receiveString();
                    std::string fullRelativePath = foldername + "\\" + relativePath;
                    std::string fileSizeStr = receiveString();
                    size_t fileSize = std::stoull(fileSizeStr);
                    
                    sendString("READY_FILE");
                    receiveFile(fullRelativePath, fileSize);
                    sendString("FILE_DONE");
                }
                
                std::cout << "Folder transfer completed in: " << getCurrentDirectory() << "\\" << foldername << std::endl;
            }
        }
    }

public:
    void start() {
        createSocket();
        listenForConnections();
        
        while (true) {
            std::cout << "\nWaiting for connection..." << std::endl;
            acceptConnection();
            handleFileTransfer();
            closesocket(client_socket);
            std::cout << "Connection closed." << std::endl;
        }
        
        closesocket(server_fd);
        WSACleanup();
    }
};

int main() {
    FileTransferServer server;
    std::cout << "=== File Transfer Server ===" << std::endl;
    server.start();
    return 0;
}