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
      RingIterator curr; // points to the most recently produced buffer, initialized to invalid
      RingIterator next; // points to the in-production buffer, initialized to invalid
      bool isClosed; // flag whether producer has shut down

      Producer(Ring *ring)
        : curr(ring)
        , next(ring)
        , isClosed(false)
      {}
    };

    struct Consumer
    {
      SwitchBufferConsumer<Buffer> *parent; // parent address used as ID
      RingIterator pos; // points to the in-consumption buffer, initialized to invalid
      bool isFull; // flag whether ring is full of consumable slots
      bool isEmpty; // flag whether ring is empty of consumable slots
      std::unique_ptr<Buffer> sanctuary; // storage to save in-consumption buffer before being overwritten
      std::unique_ptr<std::promise<Buffer const &>> promise; // promise to fulfill after empty ring

      Consumer(SwitchBufferConsumer<Buffer> *parent, Ring *ring)
        : parent(parent)
        , pos(ring)
        , isFull(false)
        , isEmpty(true)
        , sanctuary(new Buffer)
      {}

      Consumer(Consumer &&other)
        : parent(other.parent)
        , pos(other.pos)
        , isFull(other.isFull)
        , isEmpty(other.isEmpty)
        , sanctuary(std::move(other.sanctuary))
        , promise(std::move(other.promise))
      {}

      Consumer &operator=(Consumer &&other)
      {
        parent = other.parent;
        pos = other.pos;
        isFull = other.isFull;
        isEmpty = other.isEmpty;
        sanctuary = std::move(other.sanctuary);
        promise = std::move(other.promise);
        return *this;
      }
    };

    struct Consumers : public std::vector<Consumer>
    {
      typename std::vector<Consumer>::iterator find(SwitchBufferConsumer<Buffer> *parent)
      {
        return std::find_if(this->begin(), this->end(),
          [&](Consumer const &consumer) -> bool
          {
            return (parent == consumer.parent);
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
      assert(consumers.empty());
    }

    void CreateConsumer(SwitchBufferConsumer<Buffer> *parent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      consumers.emplace_back(Consumer(parent, &ring));
    }

    void CloseProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      producer.isClosed = true;

      // if there are open promises, break them
      for (auto &&consumer : consumers)
        consumer.promise.reset();
    }

    void CloseConsumer(SwitchBufferConsumer<Buffer> *parent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto const it = consumers.find(parent);
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
          std::swap(*producer.next, consumer.sanctuary);
        }
      }

      // notify Consumers if something has been produced yet
      // (the first call only provides the first buffer to the Producer)
      if (producer.curr) {
        for (auto &&consumer : consumers) {
          if (consumer.promise) {
            assert(consumer.isEmpty);
            consumer.pos = producer.curr;

            // fulfill open promise
            consumer.promise->set_value(**consumer.pos);
            consumer.promise.reset();
          } else {
            consumer.isEmpty = false;
          }
        }
      }

      return **producer.next;
    }

    std::future<Buffer const &> SwitchConsumer(SwitchBufferConsumer<Buffer> *parent, bool skipToMostRecent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      // determine consumer storage
      auto const it = consumers.find(parent);
      assert(it != std::end(consumers));
      auto &&consumer = *it;

      if (producer.curr && !consumer.isEmpty) {
        // advance ring iterator
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
{
  m_impl->CreateConsumer(this);
}


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
