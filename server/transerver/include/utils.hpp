#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include "json.hpp"
#include "client.hpp"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#endif

#define CONFIGFN ".config.json"

// 获取当前程序路径

std::string getConfigPath()
{
    std::string path;

#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    path = buffer;
    // 找到最后一个 '\' 并截断
    size_t pos = path.find_last_of("\\");
    if (pos != std::string::npos)
    {
        path = path.substr(0, pos + 1) + CONFIGFN;
    }
    else
    {
        path += "\\" + std::string(CONFIGFN);
    }
#else
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    if (count == -1)
        return "";
    path = std::string(buffer, count);
    // 找到最后一个 '/' 并截断
    size_t pos = path.find_last_of("/");
    if (pos != std::string::npos)
    {
        path = path.substr(0, pos + 1) + CONFIGFN;
    }
    else
    {
        path += "/" + std::string(CONFIGFN);
    }
#endif

    return path;
}
// 跨平台密码输入函数（显示*）
std::string get_password_input()
{
    std::string password;

#ifdef _WIN32
    // Windows 实现
    char c;
    while ((c = _getch()) != '\r')
    { // 回车结束
        if (c == '\b')
        { // 处理退格键
            if (!password.empty())
            {
                password.pop_back();
                std::cout << "\b \b"; // 删除前一个*
            }
        }
        else
        {
            password.push_back(c);
            std::cout << '*';
        }
    }
#else
    // Linux/macOS 实现
    termios oldt;
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~ECHO; // 关闭回显
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != '\n')
    {
        if (c == 127)
        { // 处理退格键
            if (!password.empty())
            {
                password.pop_back();
                std::cout << "\b \b";
            }
        }
        else
        {
            password.push_back(c);
            std::cout << '*';
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // 恢复终端设置
#endif

    std::cout << std::endl;
    return password;
}

using json = nlohmann::json;

// 保存配置
void save_config(const std::string &url, const std::string &key, const std::string &model)
{
    json config;
    config["url"] = url;
    config["key"] = key;
    config["model"] = model;
    config["temperature"] = 1.0; // 默认温度

    std::string filename = getConfigPath();
    std::ofstream file(filename);
    if (!file)
    {
        throw std::runtime_error("无法打开配置文件");
    }
    file << config.dump(4);
}

// 加载配置
bool load_config(std::string &url, std::string &key, std::string &model, float &temperature)
{
    std::string filename = getConfigPath();
    std::ifstream file(filename);

    if (!file)
        return false;

    try
    {
        json config = json::parse(file);
        url = config["url"];
        key = config["key"];
        model = config["model"];
        temperature = config["temperature"];

        // 验证字符串内容非空
        if (url.empty())
        {
            std::cerr << "配置错误：url 不能为空\n";
            return false;
        }
        if (key.empty())
        {
            std::cerr << "配置错误：key 不能为空\n";
            return false;
        }
        if (model.empty())
        {
            std::cerr << "配置错误：model 不能为空\n";
            return false;
        }

        return true;
    }
    catch (const json::exception &e)
    {
        std::cerr << "配置文件损坏: " << e.what() << "\n";
        return false;
    }
}

bool modify_conf(const std::string &conf)
{
    std::string filename = getConfigPath();

    try
    {
        // 读取文件
        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "无法打开配置文件: " << filename << "\n";
            return false;
        }

        // 解析JSON
        json config = json::parse(file);
        file.close(); // 显式关闭输入流

        // 检查配置项是否存在
        if (!config.contains(conf))
        {
            std::cerr << "配置项 '" << conf << "' 不存在\n";
            return false;
        }

        // 获取新值
        std::cout << "请输入新的 " << conf << " 值: ";
        std::string new_value;
        std::cin >> new_value;

        // 类型校验
        if (config[conf].is_number())
        {
            try
            {
                // 尝试转换为数值类型
                double num = std::stod(new_value);
                config[conf] = num;
            }
            catch (...)
            {
                std::cerr << "错误: 该配置项需要数值类型\n";
                return false;
            }
        }
        else
        {
            // 默认按字符串处理
            config[conf] = new_value;
        }

        // 写入文件
        std::ofstream out_file(filename, std::ios::trunc);
        if (!out_file)
        {
            throw std::runtime_error("无法写入配置文件");
        }
        out_file << config.dump(4);
        out_file.close();

        return true; // 修改成功
    }
    catch (const json::exception &e)
    {
        std::cerr << "JSON解析错误: " << e.what() << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "操作失败: " << e.what() << "\n";
    }
    return false;
}
// 输入URL和key

void set_conf(std::string &url,
              std::string &key, std::string &model)
{
    std::cout << "API URL: " << std::endl;
    std::cin >> url;
    std::cout << "API KEY: " << std::endl;
    key = get_password_input();
    std::cout << "Model: " << std::endl;
    std::cin >> model;
    save_config(url, key, model); // 保存配置
}

// 将参数转换为字符串
std::string params_to_string(int argc, char *argv[])
{
    std::string full_cmd;
    // 从第二个参数开始
    for (int i = 1; i < argc; ++i)
    {
        full_cmd += argv[i]; // 追加当前参数
        if (i != argc - 1)
        {
            full_cmd += " "; // 参数间加空格（最后一个不加）
        }
    }
    return full_cmd;
}

// 用于查找参数是否在vector中
bool find_in_vector(const std::vector<std::string> &vec, const std::string &target)
{
    return std::find(vec.begin(), vec.end(), target) != vec.end();
}

// 过滤非DOS兼容字符的函数
std::string filter_for_dos(const std::string &input)
{
    std::string output;
    for (char c : input)
    {
        // 处理智能引号（常见于Windows文本）
        if (static_cast<unsigned char>(c) == 0x91 || static_cast<unsigned char>(c) == 0x92)
        {
            output += '\''; // 替换为ASCII单引号
        }
        // 处理智能双引号
        else if (static_cast<unsigned char>(c) == 0x93 || static_cast<unsigned char>(c) == 0x94)
        {
            output += '"'; // 替换为ASCII双引号
        }
        // 保留可打印的ASCII字符（32-126）
        else if (c >= 32 && c <= 126)
        {
            output += c;
        }
        // 保留换行符和制表符
        else if (c == '\n' || c == '\r' || c == '\t')
        {
            output += c;
        }
        // 其他字符（包括表情符号）替换为空格
        else
        {
            output += ' ';
        }
    }
    return output;
}