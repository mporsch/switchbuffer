#ifndef SWITCHBUFFER_IMPL_H
#define SWITCHBUFFER_IMPL_H

#ifndef SWITCHBUFFER_H
# error Include this file via switchbuffer.h only
#endif

#include <cassert>
#include <map>
#include <mutex>
#include <vector>

namespace detail
{
  template<typename Buffer>
  struct SwitchBufferImpl
  {
    using Ring = std::vector<std::unique_ptr<Buffer>>;

    struct Producer
    {
      typename Ring::size_type pos;
      bool isFirst;
      bool isClosed;

      Producer()
        : pos(0U)
        , isFirst(true)
        , isClosed(false)
      {}
    };

    struct Consumer
    {
      typename Ring::size_type pos;
      bool isFirst;
      bool isFull;
      bool isEmpty;
      std::unique_ptr<Buffer> buffer;
      std::unique_ptr<std::promise<Buffer const &>> promise;

      Consumer(typename Ring::size_type first)
        : pos(first)
        , isFirst(true)
        , isFull(false)
        , isEmpty(true)
        , buffer(new Buffer)
      {}

      Consumer(Consumer &&other)
        : pos(other.pos)
        , isFirst(other.isFirst)
        , isFull(other.isFull)
        , isEmpty(other.isEmpty)
        , buffer(std::move(other.buffer))
        , promise(std::move(other.promise))
      {}
    };
    using ConsumerMap = std::map<SwitchBufferConsumer<Buffer> *, Consumer>;

    Ring ring;
    Producer producer;
    ConsumerMap consumers;
    std::mutex mtx;

    SwitchBufferImpl(size_t count)
    {
      ring.reserve(count);
      for (size_t i = 0; i < count; ++i)
        ring.emplace_back(new Buffer);
    }

    ~SwitchBufferImpl()
    {
      // the consumers must have been destroyed beforehand
      assert(consumers.empty());
    }

    void CreateConsumer(SwitchBufferConsumer<Buffer> *iface)
    {
      std::lock_guard<std::mutex> lock(mtx);

      (void)consumers.emplace(std::make_pair(iface, Consumer(producer.pos)));
    }

    void CloseProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      producer.isClosed = true;

      // if there are open promises, break them
      for (auto &&consumer : consumers)
        consumer.second.promise.reset();
    }

    void CloseConsumer(SwitchBufferConsumer<Buffer> *iface)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto it = consumers.find(iface);
      assert(it != end(consumers));
      (void)consumers.erase(it);
    }

    Buffer &SwitchProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      if (producer.isFirst) {
        producer.isFirst = false;
      } else {
        IncrementProducer();

        // if there are open promises, fulfill them
        for (auto &&p : consumers) {
          auto &&consumer = p.second;
          if (consumer.promise) {
            consumer.promise->set_value(*ring[consumer.pos]);
            consumer.promise.reset();
          }
        }
      }

      return *ring[producer.pos];
    }

    std::future<Buffer const &> SwitchConsumer(SwitchBufferConsumer<Buffer> *iface)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto &&consumer = consumers.at(iface);

      if (consumer.isFirst) {
        consumer.isFirst = false;

        // create a promise to fulfill on next Production
        consumer.promise.reset(new std::promise<Buffer const &>());
        return consumer.promise->get_future();
      } else {
        IncrementConsumer(consumer);

        if (consumer.isEmpty) {
          if (producer.isClosed) {
            // create a promise to be broken immediately
            return std::promise<Buffer const &>().get_future();
          } else {
            // create a promise to fulfill on next Production
            consumer.promise.reset(new std::promise<Buffer const &>());
            return consumer.promise->get_future();
          }
        } else {
          std::promise<Buffer const &> p;
          p.set_value(*ring[consumer.pos]);
          return p.get_future();
        }
      }
    }

    void IncrementProducer()
    {
      Increment(producer.pos);

      for (auto &&p : consumers) {
        auto &&consumer = p.second;
        if (consumer.pos == producer.pos) {
          consumer.isFull = true;

          std::swap(ring[producer.pos], consumer.buffer);
        }
      }
    }

    void IncrementConsumer(Consumer &consumer)
    {
      if (consumer.isFull) {
        consumer.isFull = false;
        consumer.pos = producer.pos;
      }
      Increment(consumer.pos);

      consumer.isEmpty = (consumer.pos == producer.pos);
    }

    void Increment(typename Ring::size_type &pos)
    {
      ++pos;
      pos %= ring.size();
    }
  };
} // namespace detail

template<typename Buffer>
SwitchBufferProducer<Buffer>::~SwitchBufferProducer()
{
  m_impl->CloseProducer();
}

template<typename Buffer>
Buffer &SwitchBufferProducer<Buffer>::Switch()
{
  return m_impl->SwitchProducer();
}

template<typename Buffer>
SwitchBufferProducer<Buffer>::SwitchBufferProducer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
  : m_impl(std::move(impl))
{}


template<typename Buffer>
SwitchBufferConsumer<Buffer>::~SwitchBufferConsumer()
{
  m_impl->CloseConsumer(this);
}

template<typename Buffer>
std::future<Buffer const &> SwitchBufferConsumer<Buffer>::Switch()
{
  return m_impl->SwitchConsumer(this);
}

template<typename Buffer>
SwitchBufferConsumer<Buffer>::SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
  : m_impl(std::move(impl))
{}


template<typename Buffer>
SwitchBuffer<Buffer>::SwitchBuffer(size_t count)
  : m_impl(std::shared_ptr<detail::SwitchBufferImpl<Buffer>>(new detail::SwitchBufferImpl<Buffer>(count)))
  , m_producer(new SwitchBufferProducer<Buffer>(m_impl))
{}

template<typename Buffer>
SwitchBuffer<Buffer>::~SwitchBuffer()
{}

template<typename Buffer>
std::unique_ptr<SwitchBufferProducer<Buffer>> SwitchBuffer<Buffer>::GetProducer()
{
  if (!m_producer)
    throw std::logic_error("SwitchBuffer: only one producer supported");
  else
    return std::move(m_producer);
}

template<typename Buffer>
std::unique_ptr<SwitchBufferConsumer<Buffer>> SwitchBuffer<Buffer>::GetConsumer()
{
  auto ret = std::unique_ptr<SwitchBufferConsumer<Buffer>>(new SwitchBufferConsumer<Buffer>(m_impl));
  m_impl->CreateConsumer(ret.get());
  return ret;
}

#endif // SWITCHBUFFER_IMPL_H
