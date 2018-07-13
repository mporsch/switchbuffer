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

template<typename Buffer>
class SwitchBufferProducer
{
  friend class SwitchBuffer<Buffer>;

public:
  ~SwitchBufferProducer();
  Buffer &Switch();

private:
  SwitchBufferProducer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
};


template<typename Buffer>
class SwitchBufferConsumer
{
  friend class SwitchBuffer<Buffer>;

public:
  ~SwitchBufferConsumer();
  std::future<Buffer const &> Switch();

private:
  SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
};


template<typename Buffer>
class SwitchBuffer
{
public:
  SwitchBuffer(size_t count);
  ~SwitchBuffer();
  std::unique_ptr<SwitchBufferProducer<Buffer>> GetProducer();
  std::unique_ptr<SwitchBufferConsumer<Buffer>> GetConsumer();

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
  std::unique_ptr<SwitchBufferProducer<Buffer>> m_producer;
};

#include "switchbuffer_impl.h"

#endif // SWITCHBUFFER_H

