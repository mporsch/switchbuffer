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

/// @brief  interface to pass to the producer:
///         provides non-blocking access to the underlying buffers
///         and publishes to the consumers
/// @note  create via the SwitchBuffer class
template<typename Buffer>
class SwitchBufferProducer
{
  friend class SwitchBuffer<Buffer>;

public:
  SwitchBufferProducer(SwitchBufferProducer const &) = delete;
  SwitchBufferProducer(SwitchBufferProducer &&other) noexcept;
  ~SwitchBufferProducer();

  SwitchBufferProducer &operator=(SwitchBufferProducer const &) = delete;
  SwitchBufferProducer &operator=(SwitchBufferProducer &&other) noexcept;

  /// @brief  get a writable buffer to produce into
  /// @note  all but the initial call also publish the previous buffer to the consumers
  Buffer &Switch();

private:
  /// created by SwitchBuffer only
  SwitchBufferProducer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
};

/// @brief  interface to pass to a consumer:
///         provides possibly-blocking access to the underlying buffers via Switch method
/// @note  create via the SwitchBuffer class
template<typename Buffer>
class SwitchBufferConsumer
{
  friend class SwitchBuffer<Buffer>;

public:
  SwitchBufferConsumer(SwitchBufferConsumer const &) = delete;
  SwitchBufferConsumer(SwitchBufferConsumer &&other) = delete;
  ~SwitchBufferConsumer();

  SwitchBufferConsumer &operator=(SwitchBufferConsumer const &) = delete;
  SwitchBufferConsumer &operator=(SwitchBufferConsumer &&other) = delete;

  /// @brief  get a readable buffer to consume from
  /// @param[in]  skipToMostRecent  false to switch the the next buffer in the queue,
  ///                               true to switch to the most recent buffer in the queue and
  ///                               permanently skip all intermediates
  std::future<Buffer const &> Switch(bool skipToMostRecent = false);

private:
  /// created by SwitchBuffer only
  SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl);

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
};

/// SwitchBuffer master interface to distribute producer and consumer interfaces
template<typename Buffer>
class SwitchBuffer
{
public:
  using Producer = typename std::unique_ptr<SwitchBufferProducer<Buffer>>;
  using Consumer = typename std::unique_ptr<SwitchBufferConsumer<Buffer>>;

public:
  SwitchBuffer(size_t ringBufferSize);
  SwitchBuffer(SwitchBuffer const &) = delete;
  SwitchBuffer(SwitchBuffer &&other) noexcept;
  ~SwitchBuffer();

  SwitchBuffer &operator=(SwitchBuffer const &) = delete;
  SwitchBuffer &operator=(SwitchBuffer &&other) noexcept;

  /// get an interface to pass to the producer
  Producer GetProducer();

  /// get an interface to pass to a consumer
  Consumer GetConsumer();

private:
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> m_impl;
  Producer m_producer;
};

#include "switchbuffer_impl.h"

#endif // SWITCHBUFFER_H
