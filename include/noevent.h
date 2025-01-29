#pragma once

#include <functional>
#include <chrono>
#include <unordered_map>
#include <memory>
#include <list>
#include <vector>
#include <queue>
#include <bitset>
#include <optional>

#include <unistd.h>


namespace noevent
{

// #define DEBUG

namespace utils
{

class Singleton
{
public:
    Singleton(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    virtual ~Singleton() = default;

protected:
    Singleton() = default;
};

class EventMinHeap
{
public:
    void Push(int fd);
    void Pop();
    void Remove(int fd);  // to be optimized...

    int Top() const { return data_.at(0); }
    bool Empty() const { return data_.empty(); }
    int Size() const { return data_.size(); }

private:
    std::vector<int> data_;
};

}  // namespace noevent::utils


namespace internal
{

class SystemEventOperation
{
public:
    SystemEventOperation() = default;

    virtual ~SystemEventOperation() { if (sys_evop_fd_ != -1) close(sys_evop_fd_); }

    virtual void Add(int fd) = 0;
    virtual void Del(int fd) = 0;
    virtual void Poll(std::chrono::seconds waitting_time) = 0;

protected:
    int sys_evop_fd_ { -1 };
};

#ifdef __linux__
class Epoll: public SystemEventOperation
{
    /// \todo implementations.
};
#endif

#ifdef __APPLE__
class KQueue final : public SystemEventOperation
{
public:
    KQueue();

    virtual ~KQueue() = default;

    virtual void Add(int fd) override;
    virtual void Del(int fd) override;
    virtual void Poll(std::chrono::seconds waitting_time) override;

private:
    int registered_event_count_ { 0 };
};
#endif

#ifdef _WIN32
class Select final : public SystemEventOperation
{
    /// \todo: implementations.
};
#endif

}  // namespace noevent::internal


class EventHub;
class Event
{
public:
    friend class EventHub;

    enum class Type
    {
        kWrite,
        kRead,
        kTimeout,
        kError,
    };
    using Callback = std::function<void(int, Type, std::shared_ptr<void>)>;

    Event(int fd) : fd_ { fd } {}

private:
    friend class utils::EventMinHeap;
#ifdef __linux__
    friend class internal::Epoll;
#elif defined(__APPLE__)
    friend class internal::KQueue;
#elif defined(_WIN32)
    friend class internal::Select;
#endif

    bool is_locked { false };
    enum class Where
    {
        kInReady,
        kInTimeout,
        kInActive,
    };
    std::bitset<3> where_;

    int fd_ { -1 };
    std::shared_ptr<void> data_ { nullptr };
    std::bitset<4> result_;

    Callback write_cb_ { nullptr };
    Callback read_cb_ { nullptr };
    Callback error_cb_ { nullptr };

    std::chrono::time_point<std::chrono::system_clock> timeout_stamp_;
};


class EventHub : public utils::Singleton
{
public:
    static EventHub& Instance();

    EventHub& CreateEmpty(int fd, Event::Callback error_cb);
    EventHub& SetCurrent(int fd);
    EventHub& OnRead(Event::Callback read_cb);
    EventHub& OnWrite(Event::Callback write_cb);
    EventHub& WithData(std::shared_ptr<void> data);

    bool IsReadEnabled(int fd) const;
    bool IsWriteEnabled(int fd) const;
    bool HasData(int fd) const;
    bool IsInTimeout(int fd) const;
    bool IsInReady(int fd) const;
    bool IsInActive(int fd) const;

    int GetCurrent() const { return current_fd_; }
    int EventsCount() const { return events.size(); }

    void Ready(std::optional<std::chrono::seconds> timeout_period = std::nullopt);
    void Destroy();
    void LoopOnce(bool can_block = true);

private:
    friend class utils::EventMinHeap;
#ifdef __linux__
    friend class internal::Epoll;
#elif defined(__APPLE__)
    friend class internal::KQueue;
#elif defined(_WIN32)
    friend class internal::Select;
#endif

    EventHub();

    void PreprocessReadyEvents();
    std::chrono::seconds CalculateWaittingTime();
    void CheckTimeoutEvents();
    void ResponseActiveEvents();

    void TimeoutPush(int fd);
    void TimeoutRemove(int fd);
    void ReadyPush(int fd);
    int ReadyFrontAndPop();
    void ActivePush(int fd);
    int ActiveFrontAndPop();

    std::unordered_map<int, std::shared_ptr<Event>> events;
    int current_fd_ { -1 };
    std::queue<int> ready_fds_;
    utils::EventMinHeap timeout_heap_;
    std::queue<int> active_fds_;
    std::unique_ptr<internal::SystemEventOperation> sys_ev_op_ { nullptr };
};

#define EV_HUB  (EventHub::Instance())


}  // namespace noevent