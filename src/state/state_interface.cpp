#include "state_interface.h"

namespace astick {

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        handles = std::move(other.handles);
        state = other.state;
        owner = other.owner;
        other.state = nullptr;
        other.owner = nullptr;
    }
    return *this;
}

Subscription::~Subscription() {
    unsubscribe();
}

void Subscription::unsubscribe() {
    if (state && owner) {
        state->unsubscribeOwner(owner);
        state = nullptr;
        owner = nullptr;
        handles.clear();
    }
}

} // namespace astick
