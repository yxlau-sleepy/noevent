#include "noevent.h"

#include <stdexcept>
#include <cassert>
#include <algorithm>

#include <iostream>


#ifdef DEBUG
#include <iostream>
#include <format>
#endif


namespace noevent
{

namespace utils
{

void EventMinHeap::Push(int fd)
{
    data_.push_back(fd);
    std::push_heap(data_.begin(), data_.end(), [](int lhs, int rhs) -> bool {
        return EV_HUB.events.at(lhs)->timeout_stamp_ >
            EV_HUB.events.at(rhs)->timeout_stamp_;
    });
}

void EventMinHeap::Pop()
{
    std::pop_heap(data_.begin(), data_.end(), [](int lhs, int rhs) -> bool {
        return EV_HUB.events.at(lhs)->timeout_stamp_ >
            EV_HUB.events.at(rhs)->timeout_stamp_;
    });
    data_.pop_back();
}

void EventMinHeap::Remove(int fd)
{
    if (auto it = std::find(data_.begin(), data_.end(), fd); it != data_.end()) {
        data_.erase(it);
        std::make_heap(data_.begin(), data_.end());
    }
}

}  // namespace noevent::utils

EventHub& EventHub::Instance()
{
    static EventHub instance;
    return instance;
}

EventHub& EventHub::CreateEmpty(int fd, Event::Callback error_cb)
{
    if (fd < 0) {
        throw std::invalid_argument("[noevent] - invalid file descriptor.");
    }
    if (error_cb == nullptr) {
        // Now the error callback is necessary for an event and the error callback
        // cannot be changed once it is definite. Users can handle all possible errors
        // or exceptions during dispatch.
        throw std::invalid_argument("[noevent] - error callback cannot be nullptr.");
    }
    if (events.contains(fd)) {
        throw std::logic_error("[noevent] - file descriptor already exists.");
    }
    auto event_ptr = std::make_shared<Event>(fd);
    if (event_ptr == nullptr) {
        throw std::runtime_error("[noevent] - failed to create empty state.");
    }

    event_ptr->error_cb_ = error_cb;
    events[fd] = event_ptr;
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) created, EVENTS: #{}\n",
        event_ptr->fd_, events.size());
#endif
    return SetCurrent(fd);  // Set `current_fd_` to `fd`.
}

EventHub& EventHub::SetCurrent(int fd)
{
    if (fd < 0) {
        throw std::invalid_argument("[noevent] - invalid file descriptor.");
    }
    if (!events.contains(fd)) {
        throw std::logic_error("[noevent] - file descriptor not exists.");
    }
    if (events.at(fd)->is_locked) {
        // We only allow users to change non-active(unlocked) events. The main reasons are as follows.
        // 1. Change an active(locked) event is unsafe, especially the callbacks.
        // 2. Let a common event (without timeout period) is not necessary. Since
        //    Invoking `Ready()` method means users want to handle it later.
        //    Users can handle the event later if it is still in active queue,
        //    which just indicates that its turn has not come yet.
        // 3. Prolong the time period of an active event due to timeout is resonable, but
        //    this is worth a new method such as `Prolong()` instead of `Ready()`.
        throw std::logic_error("[noevent] - trying to change a locked event is not allowed.");
    }

    current_fd_ = fd;
    return *this;
}

EventHub& EventHub::OnRead(Event::Callback read_cb)
{
    // The `read_cb` could be `nullptr`, which means user does not care about read event.
    events.at(current_fd_)->read_cb_ = read_cb;
    return *this;
}

EventHub& EventHub::OnWrite(Event::Callback write_cb)
{
    // The `write_cb` could be `nullptr`, which means user does not care about write event.
    events.at(current_fd_)->write_cb_ = write_cb;
    return *this;
}

EventHub& EventHub::WithData(std::shared_ptr<void> data)
{
    events.at(current_fd_)->data_ = data;
    return *this;
}

bool EventHub::IsReadEnabled(int fd) const
{
    return events.at(fd)->read_cb_ != nullptr;
}

bool EventHub::IsWriteEnabled(int fd) const
{
    return events.at(fd)->write_cb_ != nullptr;
}

bool EventHub::HasData(int fd) const
{
    return events.at(fd)->data_ != nullptr;
}

bool EventHub::IsInTimeout(int fd) const
{
    return events.at(fd)->where_.test((int)Event::Where::kInTimeout);
}

bool EventHub::IsInReady(int fd) const
{
    return events.at(fd)->where_.test((int)Event::Where::kInReady);
}

bool EventHub::IsInActive(int fd) const
{
    return events.at(fd)->where_.test((int)Event::Where::kInActive);
}

void EventHub::Ready(std::optional<std::chrono::seconds> timeout_period)
{
    const auto& current_ev = events.at(current_fd_);

    if (current_ev->read_cb_ != nullptr || current_ev->write_cb_ != nullptr) {
        if (!IsInReady(current_ev->fd_)) {
            ReadyPush(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) ready, READY: #{}\n",
        current_ev->fd_, ready_fds_.size());
#endif
        }
    }

    if (timeout_period.has_value()) {
        if (IsInTimeout(current_ev->fd_)) {
            TimeoutRemove(current_ev->fd_);
        }
        current_ev->timeout_stamp_ = std::chrono::system_clock::now() + timeout_period.value();
        TimeoutPush(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) with timeout, TIMEOUT: #{}\n",
        current_ev->fd_, timeout_heap_.Size());
#endif
    }
}

void EventHub::Destroy()
{
    auto& current_ev = events.at(current_fd_);

    if (IsInReady(current_ev->fd_) || IsInTimeout(current_ev->fd_)) {
        throw std::logic_error("[noevent] - event in ready or timeout cannot be destroyed.");
    }

    events.erase(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) destroyed, EVENTS: #{}\n",
        current_ev->fd_, events.size());
#endif
    current_ev.reset();
}

void EventHub::LoopOnce(bool can_block)
{
    using namespace std::chrono_literals;

    // Preprocess.
    PreprocessReadyEvents();
    auto waitting_time = can_block ? CalculateWaittingTime() : 0s;
#ifdef DEBUG
    if (waitting_time != 0s) {
        std::cout << std::format("[noevent] - waitting time: {}\n", waitting_time);
    }
#endif

    // Events Detect.
    sys_ev_op_->Poll(waitting_time);
    CheckTimeoutEvents();

    // Response.
    ResponseActiveEvents();
}

EventHub::EventHub() :
    sys_ev_op_
    {
#ifdef __APPLE__
        std::make_unique<internal::KQueue>()
#elif defined(__linux__)
        std::make_unique<internal::Epoll>()
#endif
    }
{
    if (sys_ev_op_ == nullptr) {
        throw std::runtime_error("[noevent] - failed to initialize system event operation.");
    }
}

void EventHub::PreprocessReadyEvents()
{
    while (!ready_fds_.empty()) {
        const auto& current_ev = events.at(ReadyFrontAndPop());

        if (current_ev->write_cb_ == nullptr && current_ev->read_cb_ == nullptr) {
            // Users can cancel the event before dispatch by clearing it's read/write callback(s).
            if (IsInTimeout(current_ev->fd_)) {
                TimeoutRemove(current_ev->fd_);
            }
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) cancelled, READY: #{}, TIMEOUT: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size());
#endif
            continue;
        }

        current_ev->is_locked = true;
        try {
            sys_ev_op_->Add(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) registered, READY: #{}, TIMEOUT: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size());
#endif
        } catch (...) {
            if (IsInTimeout(current_ev->fd_)) {
                TimeoutRemove(current_ev->fd_);
            }
            current_ev->result_.reset().set((int)Event::Type::kError, true);
            ActivePush(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) on error, READY: #{}, TIMEOUT: #{}, ACTIVE: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size(), active_fds_.size());
#endif
        }
    }
}

std::chrono::seconds EventHub::CalculateWaittingTime()
{
    using namespace std::chrono_literals;

    if (timeout_heap_.Empty()) {
        return 0s;
    }
    const auto& current_ev = events.at(timeout_heap_.Top());
    auto now = std::chrono::system_clock::now();
    if (current_ev->timeout_stamp_ <= now) {
        return 0s;
    }
    return std::chrono::duration_cast<std::chrono::seconds>
        (current_ev->timeout_stamp_ - now);
}

void EventHub::CheckTimeoutEvents()
{
    auto now = std::chrono::system_clock::now();
    while (!timeout_heap_.Empty()) {
        const auto& current_ev = events.at(timeout_heap_.Top());
        if (current_ev->timeout_stamp_ > now) {
            break;
        }
        timeout_heap_.Pop();
        current_ev->where_.set((int)Event::Where::kInTimeout, false);

        if (IsInActive(current_ev->fd_)) {
            // The event is just active, not be responsed yet.
            current_ev->result_.reset().set((int)Event::Type::kTimeout, true);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) to timeout, READY: #{}, TIMEOUT: #{}, ACTIVE: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size(), active_fds_.size());
#endif
            continue;
        }

        if (current_ev->read_cb_ != nullptr || current_ev->write_cb_ != nullptr) {
            // If any of read/write callback(s) is not `nullptr`, then the event does have
            // callback(s) since it had been locked and it is still not be activated.
            sys_ev_op_->Del(current_ev->fd_);
        }

        // Now we can change states of the event safely.
        current_ev->result_.reset().set((int)Event::Type::kTimeout, true);
        ActivePush(current_ev->fd_);
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) on timeout, READY: #{}, TIMEOUT: #{}, ACTIVE: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size(), active_fds_.size());
#endif
    }
}

void EventHub::ResponseActiveEvents()
{
    while (!active_fds_.empty()) {
        const auto& current_ev = events.at(ActiveFrontAndPop());

        if (current_ev->write_cb_ != nullptr || current_ev->read_cb_ != nullptr) {
            sys_ev_op_->Del(current_ev->fd_);
        }
        Event::Callback wr_callback = current_ev->write_cb_;
        Event::Callback rd_callbcak = current_ev->read_cb_;
        current_ev->read_cb_ = current_ev->write_cb_ = nullptr;
        if (IsInTimeout(current_ev->fd_)) {
            TimeoutRemove(current_ev->fd_);
        }

        current_ev->is_locked = false;
        if (wr_callback != nullptr && current_ev->result_.test((int)Event::Type::kWrite)) {
            wr_callback(current_ev->fd_, Event::Type::kWrite, current_ev->data_);
            if (current_ev == nullptr) {
                return;
            }
        }
        if (rd_callbcak != nullptr && current_ev->result_.test((int)Event::Type::kRead)) {
            rd_callbcak(current_ev->fd_, Event::Type::kRead, current_ev->data_);
            if (current_ev == nullptr) {
                return;
            }
        }
        if (current_ev->result_.test((int)Event::Type::kError)) {
            current_ev->error_cb_(current_ev->fd_, Event::Type::kError, current_ev->data_);
            if (current_ev == nullptr) {
                return;
            }
        }
        if (current_ev->result_.test((int)Event::Type::kTimeout)) {
            current_ev->error_cb_(current_ev->fd_, Event::Type::kTimeout, current_ev->data_);
            if (current_ev == nullptr) {
                return;
            }
        }
        current_ev->result_.reset();
#ifdef DEBUG
    std::cout << std::format("[noevent] - event({}) responsed, READY: #{}, TIMEOUT: #{}, ACTIVE: #{}\n",
        current_ev->fd_, ready_fds_.size(), timeout_heap_.Size(), active_fds_.size());
#endif
    }
}

void EventHub::TimeoutPush(int fd)
{
    timeout_heap_.Push(fd);
    events.at(fd)->where_.set((int)Event::Where::kInTimeout, true);
}

void EventHub::TimeoutRemove(int fd)
{
    timeout_heap_.Remove(fd);
    events.at(fd)->where_.set((int)Event::Where::kInTimeout, false);
}

void EventHub::ReadyPush(int fd)
{
    ready_fds_.push(fd);
    events.at(fd)->where_.set((int)Event::Where::kInReady, true);
}

int EventHub::ReadyFrontAndPop()
{
    int fd = ready_fds_.front();
    ready_fds_.pop();
    events.at(fd)->where_.set((int)Event::Where::kInReady, false);
    return fd;
}

void EventHub::ActivePush(int fd)
{
    active_fds_.push(fd);
    events.at(fd)->where_.set((int)Event::Where::kInActive, true);
}

int EventHub::ActiveFrontAndPop()
{
    int fd = active_fds_.front();
    active_fds_.pop();
    events.at(fd)->where_.set((int)Event::Where::kInActive, false);
    return fd;
}

}  // namespace noevent
