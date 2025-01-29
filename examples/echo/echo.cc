#include <iostream>
#include <string>
#include <format>
#include <chrono>
#include <memory>

#include <cstdint>

#include <sys/socket.h>
#include <sys/types.h>
#ifdef __linux__
#include <string.h>
#endif
#include <arpa/inet.h>

#include <noevent.h>

using namespace noevent;
using namespace std::chrono_literals;

constexpr int kBufferSize { 512 };


void ClientReadCallback(int fd, Event::Type type, std::shared_ptr<void> data);
void ClientWriteCallback(int fd, Event::Type type, std::shared_ptr<void> data);
void ServerReadCallback(int fd, Event::Type type, std::shared_ptr<void> data);

struct ClientData
{
    ClientData(std::string ip, std::uint16_t port)
        : ip_ { ip }, port_ { port } {}

    std::string ip_;
    std::uint16_t port_;
    std::string msg_;
};


int main()
{
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(10086);
    bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 128);

    EV_HUB.CreateEmpty(server_sock, [](int, Event::Type, std::shared_ptr<void>) {})
        .OnRead(ServerReadCallback).Ready();

    while (true) {
        EV_HUB.LoopOnce();
    }
    close(server_sock);

    return 0;
}


void ClientReadCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    auto client_data = std::static_pointer_cast<ClientData>(data);
    char buffer[kBufferSize]{'\0'};
    int msg_len = read(fd, buffer, kBufferSize - 1);
    if (msg_len <= 0) {
        std::cout << std::format("Connection was closed by client [{}:{}]\n",
            client_data->ip_, client_data->port_);
        EV_HUB.SetCurrent(fd).Destroy();
        return;
    }
    buffer[msg_len] = '\0';
    client_data->msg_ = buffer;
    std::cout << std::format("Received message \"{}\" from client [{}:{}]\n",
        client_data->msg_.substr(0, msg_len - 2), client_data->ip_, client_data->port_);
    
    EV_HUB.SetCurrent(fd).OnWrite(ClientWriteCallback).Ready();
}

void ClientWriteCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    auto client_data = std::static_pointer_cast<ClientData>(data);
    write(fd, client_data->msg_.data(), client_data->msg_.length());
    std::cout << std::format("Write message \"{}\" back to client [{}:{}]\n",
        client_data->msg_.substr(0, client_data->msg_.length() - 2), client_data->ip_, client_data->port_);
    client_data->msg_.clear();

    EV_HUB.SetCurrent(fd).OnRead(ClientReadCallback).Ready(10s);
}

void ServerReadCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(fd, (sockaddr*)&client_addr, &client_addr_len);
    std::string client_ip{ inet_ntoa(client_addr.sin_addr) };
    std::uint16_t client_port = ntohs(client_addr.sin_port);
    std::cout << std::format("Accept client with {}:{}\n", client_ip, client_port);

    EV_HUB.CreateEmpty(client_sock, [](int fd, Event::Type type, std::shared_ptr<void> data) {
            auto client_data = std::static_pointer_cast<ClientData>(data);
            if (type == Event::Type::kTimeout) {
                std::cout << std::format("Client [{}:{}] is timeout, close connection\n",
                    client_data->ip_, client_data->port_);
                close(fd);
                EV_HUB.SetCurrent(fd).Destroy();
                return;
            }
        })
        .WithData(std::make_shared<ClientData>(std::move(client_ip), client_port))
        .OnRead(ClientReadCallback).Ready(10s);

    EV_HUB.SetCurrent(fd).OnRead(ServerReadCallback).Ready();
}
