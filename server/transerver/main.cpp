#include "Socket/socket.h"
#include "include/json.hpp"
#include "include/utils.hpp"
#include "include/client.hpp"
#include <vector>
#include <memory>
using namespace mysocket::socket;

std::string api_url, api_key, model, content;
float temperature = 1.0f; // 默认温度

void handle_connection(int connfd, ApiClient &apiclient);

int main()
{

    if (!load_config(api_url, api_key, model, temperature))
    {
        std::cout << "配置文件不存在或损坏，请输入API URL和KEY:\n";
        // 输入Url和key
        set_conf(api_url, api_key, model);
    }

    Socket server;
    if (!server.bind("0.0.0.0", 8080))
        return 1;
    if (!server.listen(1024))
        return 1;

    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<ApiClient>> clients;

    json request_body;

    while (true)
    {
        int connfd;
        // 接收tcp连接
        if (!(connfd = server.accept()))
        {
            continue;
        }
        // 为每个连接建立apiclient
        auto client = std::make_unique<ApiClient>(api_url,
                                                  api_key);

        // 创建线程处理连接
        // ​​connfd​​：以 ​​值捕获​​ 的方式将 connfd（客户端 socket 文件描述符）传入线程。
        // ​​client = std::move(client)​​：
        // 将 client 对象（可能是一个智能指针或可移动对象）​​移动（move）​​ 到 Lambda 中。
        // 原 client 将变为空（避免资源重复释放）。
        // 这是 C++14 引入的 ​​广义 Lambda 捕获（Generalized Lambda Capture）​​，允许在捕获时执行表达式。
        threads.emplace_back([connfd, client = std::move(client)]()
                             { handle_connection(connfd, *client); })
            .detach(); // 分离线程
    }

    return 0;
}

void handle_connection(int connfd, ApiClient &apiclient)
{
    Socket client(connfd);
    char buf[4096] = {0};
    std::vector<json> message_history;
    std::string assistant_response;
    // std::string modelname = "/model:" + model;
    // client.send(modelname.c_str(), modelname.length());

    while (client.recv(buf, sizeof(buf)) > 0)
    {
        std::string str(buf);

        // 添加用户消息到历史
        message_history.push_back({{"role", "user"}, {"content", str}});

        json request_body = {
            {"model", model},
            {"messages", message_history},
            {"temperature", 1.0},
            {"stream", true}};

        // 启动请求线程
        std::thread request_thread([&]()
                                   { apiclient.send_request(request_body.dump()); });

        // 处理流式响应
        apiclient.setRequestState(true);
        while (apiclient.processingRequest() || !apiclient.isPubbufEmpty())
        {
            std::string data = apiclient.consumePubbuf();
            if (!data.empty())
            {
                // 过滤API响应中的非DOS兼容字符
                std::string filtered_data = filter_for_dos(data);
                client.send(filtered_data.c_str(), filtered_data.length());
                std::cout << data;
            }
        }
        assistant_response = apiclient.get_last_response();
        if (!assistant_response.empty())
        {
            message_history.push_back({{"role", "assistant"},
                                       {"content", assistant_response}});
        }
        // 结束标志符
        client.send("/done", 5);
        memset(buf, 0, sizeof(buf)); // 将buf所有字节设为0
        request_thread.join();
    }
}