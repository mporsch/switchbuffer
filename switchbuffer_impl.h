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

    class RingIterator
    {
    public:
      RingIterator(Ring *ring)
        : m_ring(ring)
        , m_pos(ring->size())
      {}

      RingIterator &operator++()
      {
        ++m_pos;
        m_pos %= m_ring->size();
        return *this;
      }

      RingIterator operator++(int)
      {
        RingIterator ret = *this;
        (void)++*this;
        return ret;
      }

      RingIterator operator+(typename Ring::difference_type offset)
      {
        RingIterator ret = *this;
        for(typename Ring::difference_type i{}; i < offset; ++i)
          ++ret;
        return ret;
      }

      typename Ring::reference operator*()
      {
        return m_ring->at(m_pos);
      }

      bool operator==(RingIterator const &other)
      {
        return ((m_ring == other.m_ring) && (m_pos == other.m_pos));
      }

      bool operator!=(RingIterator const &other)
      {
        return ((m_ring != other.m_ring) || (m_pos != other.m_pos));
      }

      bool IsValid() const
      {
        return (m_pos != m_ring->size());
      }

    private:
      Ring *m_ring;
      typename Ring::size_type m_pos;
    };

    struct Producer
    {
      RingIterator curr; ///< points to the most recently produced buffer, initialized to invalid
      RingIterator next; ///< points to the in-production buffer, initialized to invalid
      bool isClosed;

      Producer(Ring *ring)
        : curr(ring)
        , next(ring)
        , isClosed(false)
      {}
    };

    struct Consumer
    {
      RingIterator pos;
      bool isFull;
      std::unique_ptr<Buffer> buffer;
      std::unique_ptr<std::promise<Buffer const &>> promise;

      Consumer(Ring *ring)
        : pos(ring)
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
      , producer(&ring)
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

      (void)consumers.emplace(std::make_pair(iface, Consumer(&ring)));
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
      ++producer.next;

      // save buffers that are currently consumed
      for (auto &&p : consumers) {
        auto &&consumer = p.second;

        if (producer.next == consumer.pos) {
          consumer.isFull = true;
          std::swap(*producer.next, consumer.buffer);
        }
      }

      if (producer.curr.IsValid()) {
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

      if (producer.curr.IsValid() && (consumer.pos != producer.curr)) {
        if (skipToMostRecent) {
          consumer.isFull = false;
          consumer.pos = producer.curr;
        } else if (consumer.isFull) {
          consumer.isFull = false;
          consumer.pos = producer.next + 1;
        } else {
          ++consumer.pos;
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
