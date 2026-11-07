#pragma once
#include <SDL3/SDL.h>
#include <mutex>
#include <cstdio>
#include <unordered_map>

// SDL events are observed without taking input away from the runtime.
class NierLifecycle {
 public:
  void Start() { SDL_AddEventWatch(&Watch, this); }
  void Stop() { SDL_RemoveEventWatch(&Watch, this); }
 private:
  std::mutex mutex_;
  std::unordered_map<SDL_JoystickID, unsigned> buttons_;
  static bool SDLCALL Watch(void* userdata, SDL_Event* event) {
    auto& self = *static_cast<NierLifecycle*>(userdata);
    if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
      std::lock_guard lock(self.mutex_);
      self.buttons_.erase(event->gdevice.which);
      return true;
    }
    if (event->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && event->type != SDL_EVENT_GAMEPAD_BUTTON_UP)
      return true;
    unsigned bit = event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK ? 1u :
                   event->gbutton.button == SDL_GAMEPAD_BUTTON_START ? 2u : 0u;
    if (!bit) return true;
    bool quit;
    {
      std::lock_guard lock(self.mutex_);
      auto& state = self.buttons_[event->gbutton.which];
      if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) state |= bit;
      else state &= ~bit;
      quit = state == 3;
    }
    if (quit) {
      std::fprintf(stderr, "NIER_INPUT SELECT_START joystick=%u immediate quit\n", event->gbutton.which);
      SDL_Event close{};
      close.type = SDL_EVENT_QUIT;
      SDL_PushEvent(&close);
    }
    return true;
  }
};
