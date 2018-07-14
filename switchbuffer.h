#ifndef SWITCHBUFFER_H
#define SWITCHBUFFER_H

#include <future>
#include <memory>

namespace detail
{
  template<typename Buffer>
  struct SwitchBufferImpl;
} // namespace detail

template<typename Buffer>
class SwitchBuffer;

/// SwitchBuffer interface to pass to the Producer
template<typename Buffer>
class SwitchBufferProducer
{
  friend class SwitchBuffer<Buffer>;

public:
  ~SwitchBufferProducer();

  /// get a buffer to produce into
  Buffer &Switch();

private:
  /// created by SwitchBuffer only
  SwitchBufferProducer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
};

/// SwitchBuffer interface to pass to a Consumer
template<typename Buffer>
class SwitchBufferConsumer
{
  friend class SwitchBuffer<Buffer>;

public:
  ~SwitchBufferConsumer();

  /// get a buffer to consume from
  std::future<Buffer const &> Switch(bool skipToMostRecent = false);

private:
  /// created by SwitchBuffer only
  SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
  size_t m_id;
};

/// SwitchBuffer master interface to distribute Producer and Consumer interfaces
template<typename Buffer>
class SwitchBuffer
{
public:
  SwitchBuffer(size_t ringBufferSize);
  ~SwitchBuffer();

  /// get an interface to pass to the Producer
  std::unique_ptr<SwitchBufferProducer<Buffer>> GetProducer();

  /// get an interface to pass to a Consumer
  std::unique_ptr<SwitchBufferConsumer<Buffer>> GetConsumer();

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
  std::unique_ptr<SwitchBufferProducer<Buffer>> m_producer;
};

#include "switchbuffer_impl.h"

#endif // SWITCHBUFFER_H
