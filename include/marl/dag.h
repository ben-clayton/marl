// Copyright 2020 The Marl Authors.
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

#ifndef marl_dag_h
#define marl_dag_h

#include "containers.h"
#include "export.h"
#include "memory.h"
#include "scheduler.h"
#include "waitgroup.h"

namespace marl {

namespace detail {

using DAGCounter = std::atomic<uint32_t>;

template <typename T>
struct DAGRunContext {
  T data;
  Allocator::unique_ptr<DAGCounter> counters;

  template <typename F>
  MARL_NO_EXPORT inline void invoke(F&& f) {
    f(data);
  }
};

template <>
struct DAGRunContext<void> {
  Allocator::unique_ptr<DAGCounter> counters;

  template <typename F>
  MARL_NO_EXPORT inline void invoke(F&& f) {
    f();
  }
};

}  // namespace detail

///////////////////////////////////////////////////////////////////////////////
// Forward declarations
///////////////////////////////////////////////////////////////////////////////
template <typename T>
class DAG;

template <typename T>
class DAGBuilder;

template <typename T>
class DAGNodeBuilder;

///////////////////////////////////////////////////////////////////////////////
// DAGBase<T>
///////////////////////////////////////////////////////////////////////////////
template <typename T>
class DAGBase {
 protected:
  friend DAGBuilder<T>;
  friend DAGNodeBuilder<T>;

  using RunContext = detail::DAGRunContext<T>;
  using Counter = detail::DAGCounter;
  using NodeIndex = size_t;
  static const constexpr size_t NumReservedNodes = 32;
  static const constexpr size_t NumReservedNumOuts = 4;
  static const constexpr size_t InvalidCounterIndex = ~static_cast<size_t>(0);
  static const constexpr NodeIndex RootIndex = 0;

  struct Node {
    std::function<void(T)> work;
    size_t counterIndex = InvalidCounterIndex;
    containers::vector<NodeIndex, NumReservedNumOuts> outs;
  };

  MARL_NO_EXPORT inline void initCounters(RunContext* ctx,
                                          Allocator* allocator);

  MARL_NO_EXPORT inline void notify(RunContext*, NodeIndex);

  containers::vector<Node, NumReservedNodes> nodes;
  containers::vector<uint32_t, NumReservedNodes> initialCounters;
};

template <typename T>
void DAGBase<T>::initCounters(RunContext* ctx, Allocator* allocator) {
  auto numCounters = initialCounters.size();
  ctx->counters = allocator->make_unique_n<Counter>(numCounters);
  for (size_t i = 0; i < numCounters; i++) {
    ctx->counters.get()[i] = {initialCounters[i]};
  }
}

template <typename T>
void DAGBase<T>::notify(RunContext* ctx, NodeIndex nodeIdx) {
  Node* node = &nodes[nodeIdx];

  if (node->counterIndex != InvalidCounterIndex) {
    auto counters = ctx->counters.get();
    auto counter = --counters[node->counterIndex];
    if (counter > 0) {
      return;  // Still waiting on more dependencies to finish.
    }
  }

  // Run this node's work.
  if (node->work) {
    ctx->invoke(node->work);
  }

  // Then trigger all outs.
  size_t numOuts = node->outs.size();
  switch (numOuts) {
    case 0:
      break;
    case 1:
      notify(ctx, node->outs[0]);
      break;
    default: {
      marl::WaitGroup wg(static_cast<unsigned int>(numOuts - 1));
      for (NodeIndex i = 1; i < numOuts; i++) {
        marl::schedule([=] {
          notify(ctx, node->outs[i]);
          wg.done();
        });
      }
      notify(ctx, node->outs[0]);
      wg.wait();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// DAGNodeBuilder<T>
///////////////////////////////////////////////////////////////////////////////
template <typename T>
class DAGNodeBuilder {
  using NodeIndex = typename DAGBase<T>::NodeIndex;

 public:
  MARL_NO_EXPORT inline DAGNodeBuilder(DAGBuilder<T>*, NodeIndex);

  template <typename F>
  MARL_NO_EXPORT inline DAGNodeBuilder then(F&&);

 private:
  friend DAGBuilder<T>;
  DAGBuilder<T>* builder;
  NodeIndex index;
};

template <typename T>
DAGNodeBuilder<T>::DAGNodeBuilder(DAGBuilder<T>* builder, NodeIndex index)
    : builder(builder), index(index) {}

template <typename T>
template <typename F>
DAGNodeBuilder<T> DAGNodeBuilder<T>::then(F&& work) {
  auto node = builder->node(std::move(work));
  builder->addDependency(*this, node);
  return node;
}

///////////////////////////////////////////////////////////////////////////////
// DAGBuilder<T>
///////////////////////////////////////////////////////////////////////////////
template <typename T>
class DAGBuilder {
 public:
  MARL_NO_EXPORT inline DAGBuilder(Allocator* allocator = Allocator::Default);

  MARL_NO_EXPORT inline DAGNodeBuilder<T> root();

  template <typename F>
  MARL_NO_EXPORT inline DAGNodeBuilder<T> node(F&& work);

  template <typename F>
  MARL_NO_EXPORT inline DAGNodeBuilder<T> node(
      F&& work,
      std::initializer_list<DAGNodeBuilder<T>> after);

  MARL_NO_EXPORT inline void addDependency(DAGNodeBuilder<T> parent,
                                           DAGNodeBuilder<T> child);

  MARL_NO_EXPORT inline Allocator::unique_ptr<DAG<T>> build();

 private:
  static const constexpr size_t NumReservedNumIns = 4;

  Allocator::unique_ptr<DAG<T>> dag;
  containers::vector<uint32_t, NumReservedNumIns> numIns;
};

template <typename T>
DAGBuilder<T>::DAGBuilder(Allocator* allocator /* = Allocator::Default */)
    : dag(allocator->make_unique<DAG<T>>()), numIns(allocator) {
  // Add root
  dag->nodes.emplace_back(DAG<T>::Node{});
  numIns.emplace_back(0);
}

template <typename T>
DAGNodeBuilder<T> DAGBuilder<T>::root() {
  return DAGNodeBuilder<T>{this, DAGBase<T>::RootIndex};
}

template <typename T>
template <typename F>
DAGNodeBuilder<T> DAGBuilder<T>::node(F&& work) {
  return node(std::forward<F>(work), {});
}

template <typename T>
template <typename F>
DAGNodeBuilder<T> DAGBuilder<T>::node(
    F&& work,
    std::initializer_list<DAGNodeBuilder<T>> after) {
  MARL_ASSERT(numIns.size() == dag->nodes.size(),
              "NodeBuilder vectors out of sync");
  auto index = dag->nodes.size();
  numIns.emplace_back(0);
  dag->nodes.emplace_back(DAG<T>::Node{std::move(work)});
  auto node = DAGNodeBuilder<T>{this, index};
  for (auto in : after) {
    addDependency(in, node);
  }
  return node;
}

template <typename T>
void DAGBuilder<T>::addDependency(DAGNodeBuilder<T> parent,
                                  DAGNodeBuilder<T> child) {
  numIns[child.index]++;
  dag->nodes[parent.index].outs.push_back(child.index);
}

template <typename T>
Allocator::unique_ptr<DAG<T>> DAGBuilder<T>::build() {
  auto numNodes = dag->nodes.size();
  MARL_ASSERT(numIns.size() == dag->nodes.size(),
              "NodeBuilder vectors out of sync");
  for (size_t i = 0; i < numNodes; i++) {
    if (numIns[i] > 1) {
      auto& node = dag->nodes[i];
      node.counterIndex = dag->initialCounters.size();
      dag->initialCounters.push_back(numIns[i]);
    }
  }
  return std::move(dag);
}

///////////////////////////////////////////////////////////////////////////////
// DAG<T>
///////////////////////////////////////////////////////////////////////////////
template <typename T = void>
class DAG : public DAGBase<T> {
 public:
  using Builder = DAGBuilder<T>;
  using NodeBuilder = DAGNodeBuilder<T>;

  MARL_NO_EXPORT inline void run(T&, Allocator* allocator = Allocator::Default);
};

template <typename T>
void DAG<T>::run(T& arg, Allocator* allocator /* = Allocator::Default */) {
  RunContext ctx{arg};
  initCounters(&ctx, allocator);
  notify(&ctx, RootIndex);
}

///////////////////////////////////////////////////////////////////////////////
// DAG<void>
///////////////////////////////////////////////////////////////////////////////
template <>
class DAG<void> : public DAGBase<void> {
 public:
  using Builder = DAGBuilder<void>;
  using NodeBuilder = DAGNodeBuilder<void>;

  MARL_NO_EXPORT inline void run(Allocator* allocator = Allocator::Default);
};

void DAG<void>::run(Allocator* allocator /* = Allocator::Default */) {
  RunContext ctx{};
  initCounters(&ctx, allocator);
  notify(&ctx, RootIndex);
}

}  // namespace marl

#endif  // marl_dag_h
