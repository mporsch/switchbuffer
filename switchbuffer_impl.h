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
      typename Ring::iterator curr; ///< points to the most recently produced buffer, initialized to invalid
      typename Ring::iterator next; ///< points to the in-production buffer, initialized to invalid
      bool isClosed;

      Producer(Ring &ring)
        : curr(std::end(ring))
        , next(std::end(ring))
        , isClosed(false)
      {}
    };

    struct Consumer
    {
      typename Ring::iterator pos;
      bool isFull;
      std::unique_ptr<Buffer> buffer;
      std::unique_ptr<std::promise<Buffer const &>> promise;

      Consumer(Ring &ring)
        : pos(std::end(ring))
        , isFull(false)
        , buffer(new Buffer)
      {}

      Consumer(Consumer &&other)
        : pos(other.pos)
        , isFull(other.isFull)
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
      : ring(count)
      , producer(ring)
    {
      for (auto &&slot : ring)
        slot.reset(new Buffer);
    }

    ~SwitchBufferImpl()
    {
      // the consumers must have been destroyed beforehand
      assert(consumers.empty());
    }

    void CreateConsumer(SwitchBufferConsumer<Buffer> *iface)
    {
      std::lock_guard<std::mutex> lock(mtx);

      (void)consumers.emplace(std::make_pair(iface, Consumer(ring)));
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

      // advance ring iterators
      producer.curr = producer.next;
      Wrap(++Wrap(producer.next));

      // save buffers that are currently consumed
      for (auto &&p : consumers) {
        auto &&consumer = p.second;

        if (producer.next == consumer.pos) {
          consumer.isFull = true;
          std::swap(*producer.next, consumer.buffer);
        }
      }

      if (IsValid(producer.curr)) {
        for (auto &&p : consumers) {
          auto &&consumer = p.second;

          if (consumer.promise) {
            // fulfill open promise
            consumer.pos = producer.curr;
            consumer.promise->set_value(**consumer.pos);
            consumer.promise.reset();
          }
        }
      }

      return **producer.next;
    }

    std::future<Buffer const &> SwitchConsumer(SwitchBufferConsumer<Buffer> *iface, bool skipToMostRecent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto &&consumer = consumers.at(iface);

      if (IsValid(producer.curr) && (consumer.pos != producer.curr)) {
        if (skipToMostRecent) {
          consumer.isFull = false;
          consumer.pos = producer.curr;
        } else if (consumer.isFull) {
          consumer.isFull = false;
          consumer.pos = Wrap(std::next(producer.next));
        } else {
          Wrap(++Wrap(consumer.pos));
        }

        std::promise<Buffer const &> p;
        p.set_value(**consumer.pos);
        return p.get_future();
      } else {
        if (producer.isClosed) {
          // create a promise to be broken immediately
          return std::promise<Buffer const &>().get_future();
        } else {
          // create a promise to fulfill on next Production
          consumer.promise.reset(new std::promise<Buffer const &>());
          return consumer.promise->get_future();
        }
      }
    }

    bool IsValid(typename Ring::iterator const &it) const
    {
      return (it != std::end(ring));
    }

    typename Ring::iterator &Wrap(typename Ring::iterator &it)
    {
      if (!IsValid(it))
        it = std::begin(ring);
      return it;
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
std::future<Buffer const &> SwitchBufferConsumer<Buffer>::Switch(bool skipToMostRecent)
{
  return m_impl->SwitchConsumer(this, skipToMostRecent);
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
