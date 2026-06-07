#include "Client.h"
#include <sstream> 


bool Client::connectFtpServer() {
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::runtime_error("无法创建套接字");
        return false;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);  // 控制端口 2100

    if (inet_pton(AF_INET, serverAddress.c_str(), &serverAddr.sin_addr) <= 0) {
        std::runtime_error("无法获取 IP 地址");
        close(clientSocket);
        return false;
    }

    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::runtime_error("无法连接到服务器");
        close(clientSocket);
        return false;
    }

    cout << "已连接到 FTP 服务器 " << serverAddress << ":" << serverPort << '\n';

    // 接收欢迎消息
    receiveResponse();
    
    sendFtpCommand("USER anonymous");
    sendFtpCommand("PASS anonymous@");
    
    return true;
}

void Client::receiveResponse() {
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);

    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        cout << buffer;
    }
}

string Client::sendFtpCommand(const string& command) {
    if (clientSocket == -1) {
        return "无法连接到服务器";
    }

    string cmd = command + "\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);

    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        string response(buffer);
        // 移除末尾的\r\n
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
        cout << response << '\n';
        return response;
    }
    
    return "链接已被服务器关闭";
}

bool Client::portMode() {
    dataSocket = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; 
    bind(dataSocket, (struct sockaddr*)&addr, sizeof(addr));
    listen(dataSocket, 1);

    // 获取实际分配的端口
    socklen_t len = sizeof(addr);
    getsockname(dataSocket, (struct sockaddr*)&addr, &len);
    int port = ntohs(addr.sin_port);

    string cmd = "PORT 127,0,0,1," + std::to_string(port/256) + "," + std::to_string(port%256);
    sendFtpCommand(cmd);
    return true;
}


bool Client::pasvMode() {
    string response = sendFtpCommand("PASV");
    
    //227 entering passive mode (h1,h2,h3,h4,p1,p2)
    size_t start = response.find('(');
    size_t end = response.find(')');
    
    if(start == string::npos || end == string::npos) {
        cout << "PASV 响应格式错误\n";
        return false;
    }
    
    string params = response.substr(start + 1, end - start - 1);
    
    // h1,h2,h3,h4,p1,p2
    int h1, h2, h3, h4, p1, p2;
    sscanf(params.c_str(), "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
    
    //port = p1*256 + p2
    pasvPort = p1 * 256 + p2;
    pasvHost = std::to_string(h1) + "." + std::to_string(h2) + "." + 
               std::to_string(h3) + "." + std::to_string(h4);
    
    cout << "被动模式：" << pasvHost << ":" << pasvPort << '\n';
    return true;
}

string Client::listFilesPassive() {
    if(!pasvMode()) {
        return "无法进入被动模式\n";
    }
    
    dataSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(dataSocket < 0) {
        return "无法创建数据套接字\n";
    }
    
    struct sockaddr_in dataAddr;
    memset(&dataAddr, 0, sizeof(dataAddr));
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = htons(pasvPort);
    inet_pton(AF_INET, pasvHost.c_str(), &dataAddr.sin_addr);
    
    if(connect(dataSocket, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) < 0) {
        close(dataSocket);
        dataSocket = -1;
        return "无法连接到数据端口 " + pasvHost + ":" + std::to_string(pasvPort) + "\n";
    }
    
    string cmd = "LIST\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        response = buffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150" && response.substr(0, 3) != "125") {
        close(dataSocket);
        dataSocket = -1;
        return "LIST 命令失败：" + response + "\n";
    }
    
    string result = "";
    while(true) {
        memset(buffer, 0, sizeof(buffer));
        bytesReceived = recv(dataSocket, buffer, sizeof(buffer) - 1, 0);
        if(bytesReceived <= 0) break;
        
        buffer[bytesReceived] = '\0';
        result += buffer;
    }
    
    close(dataSocket);
    dataSocket = -1;
    
    memset(buffer, 0, sizeof(buffer));
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
    }
    
    return result;
}

bool Client::downloadFilePassive(const string& filename) {
    if(!pasvMode()) {
        return false;
    }
    
    dataSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(dataSocket < 0) {
        return false;
    }
    
    struct sockaddr_in dataAddr;
    memset(&dataAddr, 0, sizeof(dataAddr));
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = htons(pasvPort);
    inet_pton(AF_INET, pasvHost.c_str(), &dataAddr.sin_addr);
    
    if(connect(dataSocket, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) < 0) {
        close(dataSocket);
        dataSocket = -1;
        cout << "无法连接到数据端口 " << pasvHost << ":" << pasvPort << '\n';
        return false;
    }
    
    string cmd = "RETR " + filename + "\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        response = buffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150") {
        cout << "RETR 命令失败：" + response << '\n';
        close(dataSocket);
        dataSocket = -1;
        return false;
    }
    
    string content = "";
    while(true) {
        memset(buffer, 0, sizeof(buffer));
        bytesReceived = recv(dataSocket, buffer, sizeof(buffer), 0);
        if(bytesReceived <= 0) break;
        
        content.append(buffer, bytesReceived);
    }
    
    close(dataSocket);
    dataSocket = -1;

    memset(buffer, 0, sizeof(buffer));
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
    }
    
    pasvPort = -1;
    
    std::ofstream file(filename, std::ios::binary);
    if(!file.is_open()) {
        cout << "无法创建文件：" << filename << '\n';
        return false;
    }
    
    file.write(content.c_str(), content.length());
    file.close();
    
    cout << "文件已下载：" << filename << " (" << content.length() << " 字节)\n";
    return true;
}

bool Client::uploadFilePassive(const string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if(!file.is_open()) {
        cout << "无法打开文件：" << filename << '\n';
        return false;
    }
    
    std::stringstream fileBuffer;
    fileBuffer << file.rdbuf();
    string content = fileBuffer.str();
    file.close();
    
    if(!pasvMode()) {
        return false;
    }
    
    dataSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(dataSocket < 0) {
        return false;
    }
    
    struct sockaddr_in dataAddr;
    memset(&dataAddr, 0, sizeof(dataAddr));
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = htons(pasvPort);
    inet_pton(AF_INET, pasvHost.c_str(), &dataAddr.sin_addr);
    
    if(connect(dataSocket, (struct sockaddr*)&dataAddr, sizeof(dataAddr)) < 0) {
        close(dataSocket);
        dataSocket = -1;
        cout << "无法连接到数据端口 " << pasvHost << ":" << pasvPort << '\n';
        return false;
    }
    
    string cmd = "STOR " + filename + "\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char respBuffer[SIZE_BUFFER];
    memset(respBuffer, 0, sizeof(respBuffer));
    int bytesReceived = recv(clientSocket, respBuffer, sizeof(respBuffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        respBuffer[bytesReceived] = '\0';
        response = respBuffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150") {
        cout << "STOR 命令失败：" + response << '\n';
        close(dataSocket);
        dataSocket = -1;
        return false;
    }
    
    size_t totalSent = 0;
    while(totalSent < content.length()) {
        ssize_t sent = send(dataSocket, content.c_str() + totalSent, 
                           content.length() - totalSent, 0);
        if(sent < 0) {
            cout << "发送数据失败\n";
            close(dataSocket);
            dataSocket = -1;
            return false;
        }
        totalSent += sent;
    }
    
    cout << "已发送 " << totalSent << " 字节\n";
    
    close(dataSocket);
    dataSocket = -1;
    
    memset(respBuffer, 0, sizeof(respBuffer));
    bytesReceived = recv(clientSocket, respBuffer, sizeof(respBuffer) - 1, 0);
    if(bytesReceived > 0) {
        respBuffer[bytesReceived] = '\0';
    }
    
    pasvPort = -1;
    
    cout << "文件已上传：" << filename << '\n';
    return true;
}

string Client::listFilesActive() {
    if(!portMode()) {
        return "无法进入主动模式\n";
    }
    
    string cmd = "LIST\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        response = buffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150" && response.substr(0, 3) != "125") {
        close(dataSocket);
        dataSocket = -1;
        return "LIST 命令失败：" + response + "\n";
    }
    
    int dataConn = accept(dataSocket, nullptr, nullptr);
    close(dataSocket);
    dataSocket = -1;
    
    if(dataConn < 0) {
        return "接受数据连接失败\n";
    }
    
    string result = "";
    while(true) {
        memset(buffer, 0, sizeof(buffer));
        bytesReceived = recv(dataConn, buffer, sizeof(buffer) - 1, 0);
        if(bytesReceived <= 0) break;
        
        buffer[bytesReceived] = '\0';
        result += buffer;
    }
    
    close(dataConn);
    
    memset(buffer, 0, sizeof(buffer));
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
    }
    
    return result;
}

bool Client::downloadFileActive(const string& filename) {
    if(!portMode()) {
        return false;
    }
    
    string cmd = "RETR " + filename + "\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        response = buffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150") {
        cout << "RETR 命令失败：" + response << '\n';
        close(dataSocket);
        dataSocket = -1;
        return false;
    }
    
    int dataConn = accept(dataSocket, nullptr, nullptr);
    close(dataSocket);
    dataSocket = -1;
    
    if(dataConn < 0) {
        cout << "接受数据连接失败\n";
        return false;
    }
    
    string content = "";
    while(true) {
        memset(buffer, 0, sizeof(buffer));
        bytesReceived = recv(dataConn, buffer, sizeof(buffer), 0);
        if(bytesReceived <= 0) break;
        
        content.append(buffer, bytesReceived);
    }
    
    close(dataConn);

    memset(buffer, 0, sizeof(buffer));
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if(bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
    }
    
    std::ofstream file(filename, std::ios::binary);
    if(!file.is_open()) {
        cout << "无法创建文件：" << filename << '\n';
        return false;
    }
    
    file.write(content.c_str(), content.length());
    file.close();
    
    cout << "文件已下载：" << filename << " (" << content.length() << " 字节)\n";
    return true;
}

bool Client::uploadFileActive(const string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if(!file.is_open()) {
        cout << "无法打开文件：" << filename << '\n';
        return false;
    }
    
    std::stringstream fileBuffer;
    fileBuffer << file.rdbuf();
    string content = fileBuffer.str();
    file.close();
    
    if(!portMode()) {
        return false;
    }
    
    string cmd = "STOR " + filename + "\r\n";
    send(clientSocket, cmd.c_str(), cmd.length(), 0);
    
    char respBuffer[SIZE_BUFFER];
    memset(respBuffer, 0, sizeof(respBuffer));
    int bytesReceived = recv(clientSocket, respBuffer, sizeof(respBuffer) - 1, 0);
    string response = "";
    if(bytesReceived > 0) {
        respBuffer[bytesReceived] = '\0';
        response = respBuffer;
        while(!response.empty() && (response.back() == '\r' || response.back() == '\n')) {
            response.pop_back();
        }
    }
    
    if(response.substr(0, 3) != "150") {
        cout << "STOR 命令失败：" + response << '\n';
        close(dataSocket);
        dataSocket = -1;
        return false;
    }
    
    int dataConn = accept(dataSocket, nullptr, nullptr);
    close(dataSocket);
    dataSocket = -1;
    
    if(dataConn < 0) {
        cout << "接受数据连接失败\n";
        return false;
    }
    
    size_t totalSent = 0;
    while(totalSent < content.length()) {
        ssize_t sent = send(dataConn, content.c_str() + totalSent, 
                           content.length() - totalSent, 0);
        if(sent < 0) {
            cout << "发送数据失败\n";
            close(dataConn);
            return false;
        }
        totalSent += sent;
    }
    
    cout << "已发送 " << totalSent << " 字节\n";
    
    close(dataConn);
    
    memset(respBuffer, 0, sizeof(respBuffer));
    bytesReceived = recv(clientSocket, respBuffer, sizeof(respBuffer) - 1, 0);
    if(bytesReceived > 0) {
        respBuffer[bytesReceived] = '\0';
    }
    
    cout << "文件已上传：" << filename << '\n';
    return true;
}

string Client::listFiles() {
    if (activeMode) {
        return listFilesActive();
    } else {
        return listFilesPassive();
    }
}

bool Client::downloadFile(const string& filename) {
    if (activeMode) {
        return downloadFileActive(filename);
    } else {
        return downloadFilePassive(filename);
    }
}

bool Client::uploadFile(const string& filename) {
    if (activeMode) {
        return uploadFileActive(filename);
    } else {
        return uploadFilePassive(filename);
    }
}

void Client::closeDataConnection() {
    if(dataSocket >= 0) {
        close(dataSocket);
        dataSocket = -1;
        // pasvPort 由调用者清理
    }
}

void Client::microShell() {
    string command;

    cout << "\n╔════════════════════════════════════════╗\n";
    cout << "║       欢迎使用简易 FTP 客户端            ║\n";
    cout << "╠════════════════════════════════════════╣\n";
    cout << "║ 支持命令：                              ║\n";
    cout << "║  list              - 列出远程文件       ║\n";
    cout << "║  get <文件名>       - 下载文件          ║\n";
    cout << "║  put <文件名>       - 上传文件          ║\n";
    cout << "║  cd <目录>          - 切换目录          ║\n";
    cout << "║  pwd               - 显示当前目录       ║\n";
    cout << "║  port              - 切换到主动模式     ║\n";
    cout << "║  pasv              - 切换到被动模式     ║\n";
    cout << "║  mode              - 查看当前模式       ║\n";
    cout << "║  help              - 显示帮助           ║\n";
    cout << "║  quit              - 退出               ║\n";
    cout << "╚════════════════════════════════════════╝\n\n";

    while (true) {
        cout << "ftp> ";
        std::getline(cin, command);
        
        if (command.empty()) {
            continue;
        }
        
        // 解析本地命令
        if(command == "quit" || command == "exit") {
            sendFtpCommand("QUIT");
            cout << "已断开连接\n";
            break;
        } else if(command == "list" || command == "ls") {
            string result = listFiles();
            if(result.empty()) {
                cout << "目录为空\n";
            } else {
                cout << result;
            }
        } else if(command.substr(0, 4) == "get ") {
            string filename = command.substr(4);
            if(!downloadFile(filename)) {
                cout << "下载失败：" << filename << "\n";
            }
        } else if(command.substr(0, 4) == "put ") {
            string filename = command.substr(4);
            if(!uploadFile(filename)) {
                cout << "上传失败：" << filename << "\n";
            }
        } else if(command.substr(0, 3) == "cd ") {
            string dirname = command.substr(3);
            sendFtpCommand("CWD " + dirname);
        } else if(command == "cdup" || command == "cd ..") {
            sendFtpCommand("CDUP");
        } else if(command == "pwd") {
            sendFtpCommand("PWD");
        } else if(command == "port") {
            activeMode = true;
            cout << "已切换到主动模式 (PORT)\n";
        } else if(command == "pasv") {
            activeMode = false;
            cout << "已切换到被动模式 (PASV)\n";
        } else if(command == "mode") {
            cout << "当前模式：" << (activeMode ? "主动模式 (PORT)" : "被动模式 (PASV)") << '\n';
        } else if(command == "help" || command == "?") {
            sendFtpCommand("HELP");
        } else {
            // 未知命令提示
            cout << "未知命令：" << command << "（输入 help 查看帮助）\n";
        }
    }
}


void Client::disconnect() {
    closeDataConnection();
    
    if(clientSocket != -1) {
        sendFtpCommand("QUIT");
        close(clientSocket);
        clientSocket = -1;
        cout << "已关闭链接\n";
    }
}

bool Client::connectServer() {
    return connectFtpServer();
}

string Client::sendCommand(const string& command) {
    return sendFtpCommand(command);
}