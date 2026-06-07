#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h> 
#include <signal.h>
#include <string.h>
#include <atomic>
#include <iostream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <sstream>
#include <functional>
#include <iomanip>
#include <vector>   
#include <fstream> 
#include <dirent.h>
#include <sys/stat.h> 
#include <cmath>
#include "CommandHandler.h"
#include "ThreadPool.h"

using std::cout;    using std::endl;    using std::cin;
using std::string;  using hash_map = std::unordered_map<string, string>;
using std::mutex;   using std::unique_ptr;

constexpr int SIZE_BUFFER = 4096;       // 缓冲区
constexpr int FTP_CONTROL_PORT = 2100;  // 控制端口
constexpr int FTP_DATA_PORT = 2000;     // 主动模式数据端口
constexpr int MAX_EPOLL_EVENTS = 10;    // epoll 最大事件数
constexpr int PASV_PORT_BASE = 5000;    // 被动模式端口起始值

struct FtpSession {
    int controlSock = -1;           // 控制连接套接字
    int dataSock = -1;              // 数据连接套接字
    int pasvListenSock = -1;        // PASV 监听套接字
    int pasvPort = -1;              // 被动模式端口
    string clientIp = "";           // 客户端 IP
    string workingDir = "/";        // 当前工作目录
    bool pasvMode = false;          // 是否被动模式
    bool portMode = false;          // 是否主动模式
    string portIp = "";             // 主动模式客户端 IP
    int portPort = -1;              // 主动模式客户端端口
};

class Server {
    int controlServerSocket;                    // 控制连接监听套接字
    int dataServerSocket;                       // 数据连接监听套接字（主动模式）
    int epollFd;                                // epoll 
    int controlPort;                            // 控制端口
    int dataPort;                               // 数据端口
    std::atomic<bool> running{true};            // 运行状态
    std::atomic<bool> shutdownFlag{false};      // 延迟关闭标志
    unique_ptr<ThreadPool> threadPool;          // 线程池
    mutex sessionMutex;                         // 会话锁
    mutex dataPortMutex;                        // 数据端口分配锁
    std::unordered_map<int, FtpSession*> sessions;  // 套接字映射
    std::string serverIp;                           // 服务器 IP
    int nextPasvPort;                               // 下一个被动端口

public:
    Server(int controlPort = FTP_CONTROL_PORT, int dataPort = FTP_DATA_PORT);
    
    bool start();
    void run();
    void stop();

    std::vector<string> getLocalIps();
    
    void handleClientData(int clientSock);
    void handleFtpCommand(int clientSock, const string& command);
    void handlePasvCommand(int clientSock);
    void handlePortCommand(int clientSock, const string& args);
    void handleListCommand(int clientSock);
    void handleRetrCommand(int clientSock, const string& filename);
    void handleStorCommand(int clientSock, const string& filename);
    void handleCwdCommand(int clientSock, const string& path);
    void handleCdupCommand(int clientSock);
    void handleShutdownCommand(int clientSock);
    int getDataConnection(int clientSock, FtpSession* session); 
    
    void sendDataToClient(int dataSock, const string& data);
    string receiveDataFromClient(int dataSock, size_t maxBytes);
    string listDirectory(const string& path);
    bool saveFile(const string& path, const string& content);
    string readFile(const string& path);
    void removeSession(int clientSock);
    string getServerIp();
    string getWorkingDir(int clientSock);
    int allocatePasvPort();
    void releasePasvPort(int port);

    void printServerInfo();
    
    bool isRunning() const {return running;};
    int getControlPort() const {return controlPort;};
    int getSessionCount();
    

    ~Server() {
        stop();
    }
};

#endif