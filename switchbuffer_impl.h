#ifndef SWITCHBUFFER_IMPL_H
#define SWITCHBUFFER_IMPL_H

#ifndef SWITCHBUFFER_H
# error Include this file via switchbuffer.h only
#endif

#include <algorithm>
#include <cassert>
#include <iterator>
#include <mutex>
#if __cplusplus >= 201703L
# include <optional>
#endif // __cplusplus >= 201703L
#include <vector>

namespace detail
{
#if __cplusplus >= 201703L
  using std::optional;
#else
  template<typename T>
  struct optional
  {
    template<typename... Args>
    T &emplace(Args&&... args)
    {
      m_ptr.reset(new T(std::forward<Args>(args)...));
      return *m_ptr;
    }

    void reset() noexcept
    {
      m_ptr.reset();
    }

    T *operator->()
    {
      return m_ptr.get();
    }

    operator bool() const noexcept
    {
      return !!m_ptr;
    }

  private:
    std::unique_ptr<T> m_ptr;
  };
#endif // __cplusplus >= 201703L

  template<typename Buffer>
  struct SwitchBufferImpl
  {
    using Ring = std::vector<std::unique_ptr<Buffer>>;

    class RingIterator
      : public std::iterator<std::input_iterator_tag, typename Ring::value_type>
    {
    public:
      RingIterator(Ring *ring)
        : m_ring(ring)
        , m_pos(ring->size()) // initialize with invalid position index
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
        assert(m_ring == other.m_ring);
        return (m_pos == other.m_pos);
      }

      // check for valid position index
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
      RingIterator olde; // points to the oldest produced buffer, initialized to invalid
      bool isClosed; // flag whether producer has shut down

      Producer(Ring *ring)
        : curr(ring)
        , next(ring)
        , olde(ring)
        , isClosed(false)
      {}
    };

    struct Consumer
    {
      SwitchBufferConsumer<Buffer> const *parent; // parent address used as ID
      RingIterator pos; // points to the in-consumption buffer, initialized to invalid
      bool isFull; // flag whether ring is full of consumable slots
      bool isEmpty; // flag whether ring is empty of consumable slots
      std::unique_ptr<Buffer> sanctuary; // storage to save in-consumption buffer before being overwritten
      optional<std::promise<Buffer const &>> promise; // promise to fulfill after empty ring

      Consumer(SwitchBufferConsumer<Buffer> const *parent, Ring *ring)
        : parent(parent)
        , pos(ring)
        , isFull(false)
        , isEmpty(true)
        , sanctuary(new Buffer)
      {}

      Consumer(const Consumer &other) = delete;
      Consumer(Consumer &&other) noexcept = default;
      ~Consumer() = default;

      Consumer &operator=(const Consumer &other) = delete;
      Consumer &operator=(Consumer &&other) noexcept = default;

      // for use with std::find
      bool operator==(SwitchBufferConsumer<Buffer> const *otherParent) const
      {
        return (parent == otherParent);
      }
    };
    using Consumers = std::vector<Consumer>;

    Ring ring;
    Producer producer;
    Consumers consumers;
    std::mutex mtx;

    SwitchBufferImpl(size_t ringBufferSize)
      : ring(ringBufferSize)
      , producer(&ring)
    {
      if (ringBufferSize <= 1U)
        throw std::logic_error("SwitchBuffer: ring buffer size must be larger than 1");

      for (auto &&slot : ring)
        slot.reset(new Buffer);
    }

    SwitchBufferImpl(const SwitchBufferImpl &) = delete;
    SwitchBufferImpl(SwitchBufferImpl &&) = delete;

    ~SwitchBufferImpl()
    {
      assert(consumers.empty());
    }

    SwitchBufferImpl &operator=(const SwitchBufferImpl &) = delete;
    SwitchBufferImpl &operator=(SwitchBufferImpl &&) = delete;

    void CreateConsumer(SwitchBufferConsumer<Buffer> const *parent)
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

    void CloseConsumer(SwitchBufferConsumer<Buffer> const *parent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      auto const it = std::find(std::begin(consumers), std::end(consumers), parent);
      assert(it != std::end(consumers));
      (void)consumers.erase(it);
    }

    Buffer &SwitchProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      // advance ring iterators
      producer.curr = producer.next;
      ++producer.next;

      if (producer.curr) {
        // keep track of the oldest produced buffer
        producer.olde = (producer.olde ? std::next(producer.olde) : producer.curr);

        // notify consumers that something has been produced
        for (auto &&consumer : consumers) {
          if (consumer.promise) {
            assert(consumer.isEmpty);
            consumer.pos = producer.curr;

            // fulfill open promise
            consumer.promise->set_value(**consumer.pos);
            consumer.promise.reset();
          } else {
            consumer.isEmpty = false;

            if (producer.next == consumer.pos) {
              // save buffer that is currently consumed
              std::swap(*producer.next, consumer.sanctuary);

              consumer.isFull = true;
            } else if (consumer.isFull) {
              consumer.pos = producer.next;
            }
          }
        }
      } else {
        // no consumable buffer yet
      }

      return **producer.next;
    }

    std::future<Buffer const &> SwitchConsumer(
      SwitchBufferConsumer<Buffer> const *parent, bool skipToMostRecent)
    {
      std::lock_guard<std::mutex> lock(mtx);

      // determine consumer storage
      auto const it = std::find(std::begin(consumers), std::end(consumers), parent);
      assert(it != std::end(consumers));
      auto &&consumer = *it;

      if (consumer.isEmpty) {
        if (producer.isClosed) {
          // create a promise to be broken immediately
          return std::promise<Buffer const &>().get_future();
        } else {
          // create a promise to fulfill on next Production
          consumer.promise.emplace();
          return consumer.promise->get_future();
        }
      } else {
        // advance ring iterator
        if (skipToMostRecent) {
          consumer.pos = producer.curr;
          consumer.isEmpty = true;
        } else {
          consumer.pos = (consumer.pos ? std::next(consumer.pos) : producer.olde);
          consumer.isEmpty = (consumer.pos == producer.curr);
        }
        consumer.isFull = false;

        // return buffer immediately
        std::promise<Buffer const &> p;
        p.set_value(**consumer.pos);
        return p.get_future();
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
SwitchBufferProducer<Buffer>::SwitchBufferProducer(SwitchBufferProducer<Buffer> &&other) noexcept
  : m_impl(std::move(other.m_impl))
{}

template<typename Buffer>
SwitchBufferProducer<Buffer> &
SwitchBufferProducer<Buffer>::operator=(SwitchBufferProducer<Buffer> &&other) noexcept
{
  m_impl = std::move(other.m_impl);
  return *this;
}

template<typename Buffer>
Buffer &SwitchBufferProducer<Buffer>::Switch()
{
  return m_impl->SwitchProducer();
}

template<typename Buffer>
SwitchBufferProducer<Buffer>::SwitchBufferProducer(
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
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
SwitchBufferConsumer<Buffer>::SwitchBufferConsumer(
  std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
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
SwitchBuffer<Buffer>::~SwitchBuffer() = default;

template<typename Buffer>
SwitchBuffer<Buffer>::SwitchBuffer(SwitchBuffer<Buffer> &&other) noexcept
  : m_impl(std::move(other.m_impl))
{}

template<typename Buffer>
SwitchBuffer<Buffer> &
SwitchBuffer<Buffer>::operator=(SwitchBuffer<Buffer> &&other) noexcept
{
  m_impl = std::move(other.m_impl);
  return *this;
}

template<typename Buffer>
typename SwitchBuffer<Buffer>::Producer SwitchBuffer<Buffer>::GetProducer()
{
  if (!m_producer)
    throw std::logic_error("SwitchBuffer: only one producer supported");
  else
    return std::move(m_producer);
}

template<typename Buffer>
typename SwitchBuffer<Buffer>::Consumer SwitchBuffer<Buffer>::GetConsumer()
{
  return Consumer(new SwitchBufferConsumer<Buffer>(m_impl));
}

#endif // SWITCHBUFFER_IMPL_H
