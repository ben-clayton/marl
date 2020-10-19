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

#ifndef marl_sanitizers_h
#define marl_sanitizers_h

#include <stddef.h>  // size_t

// MARL_NOP_STATEMENT is a do-nothing statement. Used for XXX_ONLY() macros that
// have been disabled, so a trailing semicolon does not result in a warning.
#define MARL_NOP_STATEMENT \
  do {                     \
  } while (false)

// Define ADDRESS_SANITIZER_ENABLED to 1 if the project was built with the
// address sanitizer enabled (-fsanitize=address).
#if defined(__SANITIZE_ADDRESS__)
#define ADDRESS_SANITIZER_ENABLED 1
#else  // defined(__SANITIZE_ADDRESS__)
#if defined(__clang__)
#if __has_feature(address_sanitizer)
#define ADDRESS_SANITIZER_ENABLED 1
#endif  // __has_feature(address_sanitizer)
#endif  // defined(__clang__)
#endif  // defined(__SANITIZE_ADDRESS__)

#if ADDRESS_SANITIZER_ENABLED
extern "C" {
void __sanitizer_start_switch_fiber(void** fake_stack_save,
                                    const void* bottom,
                                    size_t size);
void __sanitizer_finish_switch_fiber(void* fake_stack_save,
                                     const void** bottom_old,
                                     size_t* size_old);
}
#endif  // ADDRESS_SANITIZER_ENABLED

// ADDRESS_SANITIZER_ONLY(X) resolves to X if ADDRESS_SANITIZER_ENABLED is
// defined to a non-zero value, otherwise ADDRESS_SANITIZER_ONLY() is stripped
// by the preprocessor.
#if ADDRESS_SANITIZER_ENABLED
#define ADDRESS_SANITIZER_ONLY(x) x
#else
#define ADDRESS_SANITIZER_ONLY(x) MARL_NOP_STATEMENT
#endif  // ADDRESS_SANITIZER_ENABLED

// Define MEMORY_SANITIZER_ENABLED to 1 if the project was built with the memory
// sanitizer enabled (-fsanitize=memory).
#if defined(__SANITIZE_MEMORY__)
#define MEMORY_SANITIZER_ENABLED 1
#else  // defined(__SANITIZE_MEMORY__)
#if defined(__clang__)
#if __has_feature(memory_sanitizer)
#define MEMORY_SANITIZER_ENABLED 1
#endif  // __has_feature(memory_sanitizer)
#endif  // defined(__clang__)
#endif  // defined(__SANITIZE_MEMORY__)

// MEMORY_SANITIZER_ONLY(X) resolves to X if MEMORY_SANITIZER_ENABLED is defined
// to a non-zero value, otherwise MEMORY_SANITIZER_ONLY() is stripped by the
// preprocessor.
#if MEMORY_SANITIZER_ENABLED
#define MEMORY_SANITIZER_ONLY(x) x
#else
#define MEMORY_SANITIZER_ONLY(x) MARL_NOP_STATEMENT
#endif  // MEMORY_SANITIZER_ENABLED

// Define THREAD_SANITIZER_ENABLED to 1 if the project was built with the thread
// sanitizer enabled (-fsanitize=thread).
#if defined(__SANITIZE_THREAD__)
#define THREAD_SANITIZER_ENABLED 1
#else  // defined(__SANITIZE_THREAD__)
#if defined(__clang__)
#if __has_feature(thread_sanitizer)
#define THREAD_SANITIZER_ENABLED 1
#endif  // __has_feature(thread_sanitizer)
#endif  // defined(__clang__)
#endif  // defined(__SANITIZE_THREAD__)

#if THREAD_SANITIZER_ENABLED
extern "C" {
void* __tsan_get_current_fiber(void);
void* __tsan_create_fiber(unsigned flags);
void __tsan_destroy_fiber(void* fiber);
void __tsan_switch_to_fiber(void* fiber, unsigned flags);
void __tsan_set_fiber_name(void* fiber, const char* name);
}
#endif

// THREAD_SANITIZER_ONLY(X) resolves to X if THREAD_SANITIZER_ENABLED is defined
// to a non-zero value, otherwise THREAD_SANITIZER_ONLY() is stripped by the
// preprocessor.
#if THREAD_SANITIZER_ENABLED
#define THREAD_SANITIZER_ONLY(x) x
#else
#define THREAD_SANITIZER_ONLY(x) MARL_NOP_STATEMENT
#endif  // THREAD_SANITIZER_ENABLED

#endif  // marl_sanitizers_h
