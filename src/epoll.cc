#include "noevent.h"

#include <stdexcept>
#include <iostream>

#ifdef __linux__
#include <sys/epoll.h>
#endif


namespace noevent::internal
{

#ifdef __linux__


Epoll::Epoll()
{
    sys_evop_fd_ = epoll_create1(0);
    if (sys_evop_fd_ == -1) {
        throw std::runtime_error("[noevent] - failed to create epoll.");
    }
}

void Epoll::Add(int fd)
{
    const auto& current_ev = EV_HUB.events.at(fd);
    struct epoll_event epoll_ev;
    epoll_ev.events = 0;
    epoll_ev.data.fd = current_ev->fd_;


    if (current_ev->write_cb_ != nullptr) {
        epoll_ev.events |= EPOLLOUT;
    }
    if (current_ev->read_cb_ != nullptr) {
        epoll_ev.events |= EPOLLIN;
    }

    if (epoll_ctl(sys_evop_fd_, EPOLL_CTL_ADD, fd, &epoll_ev)) {
        throw std::runtime_error("[noevent] - failed to add event.");
    }
    registered_event_count_++;
}

void Epoll::Del(int fd)
{
    if (epoll_ctl(sys_evop_fd_, EPOLL_CTL_DEL, fd, nullptr)) {
        throw std::runtime_error("[noevent] - failed to del event.");
    }
    registered_event_count_--;
}

void Epoll::Poll(std::chrono::seconds waitting_time)
{
    std::vector<struct epoll_event> active_epoll_evs(registered_event_count_);
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(waitting_time);

    int nactive = epoll_wait(sys_evop_fd_, active_epoll_evs.data(), active_epoll_evs.size(), ts.count());
    if (nactive < 0) {
        throw std::runtime_error("[noevent] - failed to poll events.");
    }

    for (int i = 0; i < nactive; ++i) {
        const auto& current_ev = EV_HUB.events.at(active_epoll_evs[i].data.fd);
        if (active_epoll_evs[i].events & EPOLLIN) {
            current_ev->result_.set((int)Event::Type::kRead, true);
        }
        if (active_epoll_evs[i].events & EPOLLOUT) {
            current_ev->result_.set((int)Event::Type::kWrite, true);
        }
        EV_HUB.ActivePush(current_ev->fd_);
    }
}


#endif

}  // namespace noevent::internal
