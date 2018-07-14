#ifndef SWITCHBUFFER_IMPL_H
#define SWITCHBUFFER_IMPL_H

#ifndef SWITCHBUFFER_H
# error Include this file via switchbuffer.h only
#endif

#include <algorithm>
#include <cassert>
#include <iterator>
#include <mutex>
#include <vector>

namespace detail
{
  template<typename Buffer>
  struct SwitchBufferImpl
  {
    using Ring = std::vector<std::unique_ptr<Buffer>>;

    class RingIterator : public std::iterator<std::input_iterator_tag, typename Ring::value_type>
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

      typename Ring::reference operator*()
      {
        assert(*this);
        return (*m_ring)[m_pos];
      }

      bool operator==(RingIterator const &other) const
      {
        return ((m_ring == other.m_ring) && (m_pos == other.m_pos));
      }

      operator bool() const
      {
        return (m_pos < m_ring->size());
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
      size_t id;
      RingIterator pos;
      bool isFull;
      bool isEmpty;
      std::unique_ptr<Buffer> buffer;
      std::unique_ptr<std::promise<Buffer const &>> promise;

      Consumer(size_t id, Ring *ring)
        : id(id)
        , pos(ring)
        , isFull(false)
        , isEmpty(true)
        , buffer(new Buffer)
      {}

      Consumer(Consumer &&other)
        : id(other.id)
        , pos(other.pos)
        , isFull(other.isFull)
        , isEmpty(other.isEmpty)
        , buffer(std::move(other.buffer))
        , promise(std::move(other.promise))
      {}

      Consumer &operator=(Consumer &&other)
      {
        id = other.id;
        pos = other.pos;
        isFull = other.isFull;
        isEmpty = other.isEmpty;
        buffer = std::move(other.buffer);
        promise = std::move(other.promise);
        return *this;
      }
    };

    struct Consumers : public std::vector<Consumer>
    {
      typename std::vector<Consumer>::iterator find(size_t id)
      {
        return std::find_if(this->begin(), this->end(),
          [&](Consumer const &consumer) -> bool
          {
            return (id == consumer.id);
          });
      }
    };

    Ring ring;
    Producer producer;
    Consumers consumers;
    std::mutex mtx;

    SwitchBufferImpl(size_t ringBufferSize)
      : ring(ringBufferSize)
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

    size_t CreateConsumer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      size_t const id = (consumers.empty() ? 0U : consumers.back().id + 1U);
      consumers.emplace_back(Consumer(id, &ring));
      return id;
    }

    void CloseProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      producer.isClosed = true;

      // if there are open promises, break them
      for (auto &&consumer : consumers)
        consumer.promise.reset();
    }

    void CloseConsumer(size_t id)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto const it = consumers.find(id);
      assert(it != std::end(consumers));
      (void)consumers.erase(it);
    }

    Buffer &SwitchProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      // advance ring iterators
      producer.curr = producer.next;
      ++producer.next;

      // save buffers that are currently consumed
      for (auto &&consumer : consumers) {
        if (producer.next == consumer.pos) {
          consumer.isFull = true;
          std::swap(*producer.next, consumer.buffer);
        }
      }

      if (producer.curr) {
        for (auto &&consumer : consumers) {
          // fulfill open promise
          if (consumer.promise) {
            assert(consumer.isEmpty);
            consumer.pos = producer.curr;
            consumer.promise->set_value(**consumer.pos);
            consumer.promise.reset();
          } else {
            consumer.isEmpty = false;
          }
        }
      }

      return **producer.next;
    }

    std::future<Buffer const &> SwitchConsumer(size_t id, bool skipToMostRecent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto const it = consumers.find(id);
      assert(it != std::end(consumers));
      auto &&consumer = *it;

      if (producer.curr && !consumer.isEmpty) {
        if (skipToMostRecent) {
          consumer.isFull = false;
          consumer.pos = producer.curr;
        } else if (consumer.isFull) {
          consumer.isFull = false;
          consumer.pos = std::next(producer.next);
        } else {
          ++consumer.pos;
        }

        consumer.isEmpty = (consumer.pos == producer.curr);

        // return buffer immediately
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
  m_impl->CloseConsumer(m_id);
}

template<typename Buffer>
std::future<Buffer const &> SwitchBufferConsumer<Buffer>::Switch(bool skipToMostRecent)
{
  return m_impl->SwitchConsumer(m_id, skipToMostRecent);
}

template<typename Buffer>
SwitchBufferConsumer<Buffer>::SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
  : m_impl(std::move(impl))
  , m_id(m_impl->CreateConsumer())
{}


template<typename Buffer>
SwitchBuffer<Buffer>::SwitchBuffer(size_t ringBufferSize)
  : m_impl(new detail::SwitchBufferImpl<Buffer>(ringBufferSize))
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
  return std::unique_ptr<SwitchBufferConsumer<Buffer>>(new SwitchBufferConsumer<Buffer>(m_impl));
}

#endif // SWITCHBUFFER_IMPL_H
