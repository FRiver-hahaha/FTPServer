#ifndef CMDHANDLER_H
#define CMDHANDLER_H

#include <string>
#include <unordered_map>
#include <functional>

// 注释掉原说明
/*
    用来解析命令行
    给每个命令绑定函数，输入对应的命令执行对应函数
    使用哈希表，效率高
    处理了参数不存在，参数错误等人为错误性问题
*/

using std::string;  
using hash_map = std::unordered_map<std::string, std::string>;
using cmdFunc = std::function<std::string(const std::string&)>;

class CommandHandler {

    using cmdFunc = std::function<string (const string&)>;

    std::unordered_map<string, cmdFunc> comTofunc;
    hash_map amanHelp;

public:
    CommandHandler() {
        registerCmd();
    }

    string execute(const string command);
    string help();

private:
    void registerCmd();
};

#endif