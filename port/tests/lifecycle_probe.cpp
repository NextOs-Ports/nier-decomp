#include "lifecycle.h"
#include <cstdio>

int main() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 2;
    NierLifecycle lifecycle;
    lifecycle.Start();
    auto send = [](SDL_JoystickID id, Uint8 button, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
        event.gbutton.which = id;
        event.gbutton.button = button;
        event.gbutton.down = down;
        return SDL_PushEvent(&event);
    };
    auto quits = [] {
        int count = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) ++count;
        return count;
    };
    send(1, SDL_GAMEPAD_BUTTON_BACK, true);
    send(2, SDL_GAMEPAD_BUTTON_START, true);
    if (quits() != 0) return 3;
    send(1, SDL_GAMEPAD_BUTTON_BACK, false);
    send(1, SDL_GAMEPAD_BUTTON_START, true);
    if (quits() != 0) return 4;
    send(1, SDL_GAMEPAD_BUTTON_BACK, true);
    // No held-time threshold: the second press queues quit immediately.
    if (quits() != 1) return 5;
    lifecycle.Stop();
    SDL_Quit();
    std::puts("PASS: isolated buttons/controllers do not quit; same-controller combo quits immediately");
}
