#include "noevent.h"

#include <stdexcept>
#include <iostream>

#ifdef __APPLE__
#include <sys/event.h>
#include <string.h>
#endif


namespace noevent::internal
{

#ifdef __APPLE__


KQueue::KQueue() : SystemEventOperation()
{
    sys_evop_fd_ = kqueue();
    if (sys_evop_fd_ == -1) {
        throw std::runtime_error("[noevent] - failed to create kqueue.");
    }
}

void KQueue::Add(int fd)
{
    auto current_ev = EV_HUB.events.at(fd);
    struct kevent kev;
    EV_SET(&kev, current_ev->fd_, 0, EV_ADD|EV_CLEAR, 0, 0, NULL);

    if (current_ev->write_cb_ != nullptr) {
        kev.filter = EVFILT_WRITE;
        if (kevent(sys_evop_fd_, &kev, 1, NULL, 0, NULL)) {
            throw std::runtime_error("[noevent] - failed to add event(w).");
        }
    }
    if (current_ev->read_cb_ != nullptr) {
        kev.filter = EVFILT_READ;
        if (kevent(sys_evop_fd_, &kev, 1, NULL, 0, NULL)) {
            throw std::runtime_error("[noevent] - failed to add event(r).");
        }
    }

    registered_event_count_++;
}

void KQueue::Del(int fd)
{
    auto current_ev = EV_HUB.events.at(fd);
    struct kevent kev;
    EV_SET(&kev, current_ev->fd_, 0, EV_DELETE, 0, 0, NULL);

    if (current_ev->write_cb_ != nullptr) {
        kev.filter = EVFILT_WRITE;
        if (kevent(sys_evop_fd_, &kev, 1, NULL, 0, NULL)) {
            // throw std::runtime_error("[noevent] - failed to delete event(w).");
        }
    }
    if (current_ev->read_cb_ != nullptr) {
        kev.filter = EVFILT_READ;
        if (kevent(sys_evop_fd_, &kev, 1, NULL, 0, NULL)) {
            // throw std::runtime_error("[noevent] - failed to delete event(r).");
        }
    }

    registered_event_count_--;
}

void KQueue::Poll(std::chrono::seconds waitting_time)
{
    std::vector<struct kevent> active_kevs(registered_event_count_);
    struct timespec ts { .tv_nsec = 0 };
    ts.tv_sec = static_cast<long>(waitting_time.count());

    int nactive = kevent(sys_evop_fd_, NULL, 0, active_kevs.data(), active_kevs.size(), &ts);
    if (nactive < 0) {
        throw std::runtime_error("[noevent] - failed to poll events.");
    }

    for (int i = 0; i < nactive; ++i) {
        auto current_ev = EV_HUB.events.at(active_kevs[i].ident);

        switch (active_kevs[i].filter)
        {
            case EVFILT_READ:
                current_ev->result_.set((int)Event::Type::kRead, true);
                break;
            case EVFILT_WRITE:
                current_ev->result_.set((int)Event::Type::kWrite, true);
                break;
            default:
                break;
        }
        if (!EV_HUB.IsInActive(current_ev->fd_)) {
            EV_HUB.ActivePush(current_ev->fd_);
        }
    }
}


#endif

}  // namespace noevent::internal
