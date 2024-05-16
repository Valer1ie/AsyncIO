#pragma once
#include <functional>
namespace John {
template <typename TFunc> struct Disposer {
  TFunc func;
  Disposer(TFunc &&func) : func(std::move(func)) {}
  ~Disposer() { func(); }
};
template <typename TFunc> Disposer<TFunc> OnExitScope(TFunc &&func) {
  return Disposer<TFunc>(std::move(func));
}
} // namespace John