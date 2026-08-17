#pragma once

#include <cstdint>
#include <functional>
#include <string>

using ActiveTarget = std::uintptr_t;

enum class MessageKind {
    Information,
    Warning,
    Error
};

class IApplicationPlatform {
public:
    virtual ~IApplicationPlatform() = default;

    virtual ActiveTarget capture_active_target() const = 0;
    virtual bool is_target_active(ActiveTarget target) const = 0;

    virtual void show_overlay(const std::string& text) = 0;
    virtual void hide_overlay() = 0;

    virtual void show_message(
        const std::string& title,
        const std::string& text,
        MessageKind kind
    ) = 0;

    virtual bool insert_text(
        const std::string& text,
        std::string& error
    ) = 0;

    // The callback must run on the platform UI thread. Implementations take
    // ownership of the callback only when this method returns true.
    virtual bool post_to_main(std::function<void()> callback) = 0;
};
