// Copyright 2019 The Marl Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Minimal assembly implementations of fiber context switching for Unix-based
// platforms.
//
// Note: Unlike makecontext, swapcontext or the Windows fiber APIs, these
// assembly implementations *do not* save or restore signal masks,
// floating-point control or status registers, FS and GS segment registers,
// thread-local storage state nor any SIMD registers. This should not be a
// problem as the marl scheduler requires fibers to be executed on the same
// thread throughout their lifetime.

#if defined(__x86_64__)
#include "osfiber_asm_x64.h"
#elif defined(__i386__)
#include "osfiber_asm_x86.h"
#elif defined(__aarch64__)
#include "osfiber_asm_aarch64.h"
#elif defined(__arm__)
#include "osfiber_asm_arm.h"
#elif defined(__powerpc64__)
#include "osfiber_asm_ppc64.h"
#elif defined(__mips__) && _MIPS_SIM == _ABI64
#include "osfiber_asm_mips64.h"
#else
#error "Unsupported target"
#endif

#include "marl/export.h"
#include "marl/memory.h"
#include "marl/sanitizers.h"

#include <functional>
#include <memory>

extern "C" {

MARL_EXPORT
extern void marl_fiber_set_target(marl_fiber_context*,
                                  void* stack,
                                  uint32_t stack_size,
                                  void (*target)(void*),
                                  void* arg);
MARL_EXPORT
extern void marl_fiber_swap(marl_fiber_context* from,
                            const marl_fiber_context* to);

}  // extern "C"

namespace marl {

class OSFiber {
 public:
  inline OSFiber(Allocator*);
  inline ~OSFiber();

  // createFiberFromCurrentThread() returns a fiber created from the current
  // thread.
  MARL_NO_EXPORT static inline Allocator::unique_ptr<OSFiber>
  createFiberFromCurrentThread(Allocator* allocator);

  // createFiber() returns a new fiber with the given stack size that will
  // call func when switched to. func() must end by switching back to another
  // fiber, and must not return.
  MARL_NO_EXPORT static inline Allocator::unique_ptr<OSFiber> createFiber(
      Allocator* allocator,
      size_t stackSize,
      const std::function<void()>& func);

  // switchTo() immediately switches execution to the given fiber.
  // switchTo() must be called on the currently executing fiber.
  MARL_NO_EXPORT inline void switchTo(OSFiber*);

 private:
  MARL_NO_EXPORT
  static inline void run(OSFiber* self);

  Allocator* allocator;
  marl_fiber_context context;
  std::function<void()> target;
  Allocation stack;

#if THREAD_SANITIZER_ENABLED
  struct TSAN {
    void* fiber = nullptr;
    bool owner = false;

    void pre_switch(OSFiber* to) { __tsan_switch_to_fiber(to->tsan.fiber, 0); }
    ~TSAN() {
      if (owner) {
        __tsan_destroy_fiber(fiber);
      }
    }
  } tsan;
#endif  // THREAD_SANITIZER_ENABLED

#if ADDRESS_SANITIZER_ENABLED
  struct ASAN {
    void* stack_save = nullptr;
    const void* stack_base = nullptr;
    size_t stack_size = 0;
    ASAN* switched_from = nullptr;

    void pre_switch(OSFiber* to) {
      to->asan.switched_from = this;
      __sanitizer_start_switch_fiber(&stack_save, to->asan.stack_base,
                                     to->asan.stack_size);
    }
    void post_switch() {
      __sanitizer_finish_switch_fiber(stack_save, &switched_from->stack_base,
                                      &switched_from->stack_size);
    }
    ~ASAN() {
      if (stack_save) {
        // "When leaving a fiber definitely, NULL must be passed as the first
        // argument to the __sanitizer_start_switch_fiber() function so that the
        // fake stack is destroyed."
        __sanitizer_start_switch_fiber(nullptr, stack_base, stack_size);
        __sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr);
      }
    }
  } asan;
#endif  // ADDRESS_SANITIZER_ENABLED
};

OSFiber::OSFiber(Allocator* allocator) : allocator(allocator) {}

OSFiber::~OSFiber() {
  if (stack.ptr != nullptr) {
    allocator->free(stack);
  }
}

Allocator::unique_ptr<OSFiber> OSFiber::createFiberFromCurrentThread(
    Allocator* allocator) {
  auto out = allocator->make_unique<OSFiber>(allocator);
  out->context = {};

  THREAD_SANITIZER_ONLY(out->tsan.fiber = __tsan_get_current_fiber());

  return out;
}

Allocator::unique_ptr<OSFiber> OSFiber::createFiber(
    Allocator* allocator,
    size_t stackSize,
    const std::function<void()>& func) {
  Allocation::Request request;
  request.size = stackSize;
  request.alignment = 16;
  request.usage = Allocation::Usage::Stack;
#if MARL_USE_FIBER_STACK_GUARDS
  request.useGuards = true;
#endif

  auto out = allocator->make_unique<OSFiber>(allocator);
  out->context = {};
  out->target = func;
  out->stack = allocator->allocate(request);

  marl_fiber_set_target(
      &out->context, out->stack.ptr, static_cast<uint32_t>(stackSize),
      reinterpret_cast<void (*)(void*)>(&OSFiber::run), out.get());

#if ADDRESS_SANITIZER_ENABLED
  out->asan.stack_base =
      static_cast<uint8_t*>(out->stack.ptr) - out->stack.request.size;
  out->asan.stack_size = out->stack.request.size;
#endif

#if THREAD_SANITIZER_ENABLED
  out->tsan.fiber = __tsan_create_fiber(0);
  out->tsan.owner = true;
#endif

  return out;
}

void OSFiber::run(OSFiber* self) {
  ADDRESS_SANITIZER_ONLY(self->asan.post_switch());
  self->target();
}

void OSFiber::switchTo(OSFiber* fiber) {
  THREAD_SANITIZER_ONLY(tsan.pre_switch(fiber));
  ADDRESS_SANITIZER_ONLY(asan.pre_switch(fiber));

  marl_fiber_swap(&context, &fiber->context);

  ADDRESS_SANITIZER_ONLY(asan.post_switch());
}

}  // namespace marl
