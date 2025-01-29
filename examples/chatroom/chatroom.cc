#include <iostream>
#include <string>
#include <format>
#include <chrono>
#include <queue>
#include <memory>
#include <sstream>

#include <sys/socket.h>
#include <sys/types.h>
#ifdef __linux__
#include <string.h>
#endif
#include <arpa/inet.h>

#include <noevent.h>

using namespace noevent;
using namespace std::chrono_literals;


using MsgQueue = std::queue<std::shared_ptr<std::string>>;

struct UserData
{
    UserData(std::string ip, std::uint16_t port)
        : ip_ { ip }, port_ { port } {}

    std::string ip_;
    std::uint16_t port_;
    MsgQueue msg_queue_;
    bool is_quit_ { false };
};


constexpr int kBufferSize { 512 };
std::unordered_map<int, std::shared_ptr<UserData>> accepted_users;

void ServerReadCallback(int fd, Event::Type type, std::shared_ptr<void> data);
void UserReadCallback(int fd, Event::Type type, std::shared_ptr<void> data);
void UserWriteCallback(int fd, Event::Type type, std::shared_ptr<void> data);


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

void ServerReadCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    sockaddr_in user_addr;
    socklen_t user_addr_len = sizeof(user_addr);
    int user_sock = accept(fd, (sockaddr*)&user_addr, &user_addr_len);
    std::string user_ip{ inet_ntoa(user_addr.sin_addr) };
    std::uint16_t user_port = ntohs(user_addr.sin_port);
    std::cout << std::format("Accept user with {}:{}\n", user_ip, user_port);

    auto user = std::make_shared<UserData>(std::move(user_ip), user_port);
    accepted_users[user_sock] = user;
    EV_HUB.CreateEmpty(user_sock, [](int, Event::Type, std::shared_ptr<void>) {})
        .WithData(user).OnRead(UserReadCallback).OnWrite(UserWriteCallback).Ready();

    EV_HUB.SetCurrent(fd).OnRead(ServerReadCallback).Ready();
}

void UserReadCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    auto user_data = std::static_pointer_cast<UserData>(data);
    if (user_data->is_quit_) {
        return;
    }

    char buffer[kBufferSize]{'\0'};
    int msg_len = read(fd, buffer, kBufferSize - 1);
    if (msg_len <= 0) {
        std::cout << std::format("User [{}:{}] quit.\n", user_data->ip_, user_data->port_);
        accepted_users.erase(fd);  // no longer new messages are going to be received.
        user_data->is_quit_ = true;
        return;
    }
    buffer[msg_len] = '\0';

    auto user_msg = std::make_shared<std::string>(
        std::format("[{}:{}] - ", user_data->ip_, user_data->port_) + buffer);
    for (const auto& user : accepted_users) {
        user.second->msg_queue_.push(user_msg);
    }
    std::cout << std::format("Meesage: \n\t{} from [{}:{}] has been broadcast.\n",
        buffer, user_data->ip_, user_data->port_);

    EV_HUB.SetCurrent(fd).OnRead(UserReadCallback).OnWrite(UserWriteCallback).Ready();
}

void UserWriteCallback(int fd, Event::Type type, std::shared_ptr<void> data)
{
    auto user_data = std::static_pointer_cast<UserData>(data);
    if (user_data->is_quit_) {
        EV_HUB.SetCurrent(fd).Destroy();
        return;
    }

    auto msg_count = user_data->msg_queue_.size();
    std::stringstream ss;
    while (!user_data->msg_queue_.empty()) {
        ss << *(user_data->msg_queue_.front());
        user_data->msg_queue_.pop();
    }

    std::string messages = ss.str();
    if (!messages.empty()) {
        write(fd, messages.data(), messages.length());
        std::cout << std::format("Write {} messages back to [{}:{}]\n",
            msg_count, user_data->ip_, user_data->port_);
    }

    EV_HUB.SetCurrent(fd).OnRead(UserReadCallback).OnWrite(UserWriteCallback).Ready();
}
