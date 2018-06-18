#ifndef SWITCHBUFFER_IMPL_H
#define SWITCHBUFFER_IMPL_H

#ifndef SWITCHBUFFER_H
# error Include this file via switchbuffer.h only
#endif

#include <mutex>
#include <vector>

namespace detail
{
  template<typename Buffer>
  struct SwitchBufferImpl
  {
    std::mutex mtx;
    std::vector<Buffer> ring;
    Buffer slotProducer;
    Buffer slotConsumer;
    typename std::vector<Buffer>::iterator itProducer;
    typename std::vector<Buffer>::iterator itConsumer;
    bool isFull;
    bool isEmpty;
    std::unique_ptr<std::promise<Buffer const &>> promise;
    bool isFirst;
    bool closedProducer;

    SwitchBufferImpl(size_t count)
      : ring(count - 2, Buffer())
      , itProducer(ring.begin())
      , itConsumer(ring.begin())
      , isFull(false)
      , isEmpty(true)
      , promise(nullptr)
      , isFirst(true)
      , closedProducer(false)
    {}

    ~SwitchBufferImpl()
    {}

    Buffer &SwitchProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      if (isFirst) {
        isFirst = false;
      } else {
        // if there is an open promise, fulfill it
        if (promise) {
          // immediately swap consumer and producer slot
          std::swap(slotProducer, slotConsumer);
          promise->set_value(slotConsumer);
          promise.reset();
        } else {
          IncrementProducer();

          std::swap(*itProducer, slotProducer);
        }
      }

      return slotProducer;
    }

    std::future<Buffer const &> SwitchConsumer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      if (isEmpty) {
        if (closedProducer) {
          // create a promise to be broken immediately
          return std::promise<Buffer const &>().get_future();
        } else {
          // create a promise to fulfill on next Production
          promise.reset(new std::promise<Buffer const &>());
          return promise->get_future();
        }
      } else {
        IncrementConsumer();

        std::swap(*itConsumer, slotConsumer);

        std::promise<Buffer const &> p;
        p.set_value(slotConsumer);
        return p.get_future();
      }
    }

    void CloseProducer()
    {
      std::lock_guard<std::mutex> lock(mtx);

      closedProducer = true;

      // if there is an open promise, break it
      promise.reset();
    }

    void CloseConsumer()
    {}

    void IncrementProducer()
    {
      isEmpty = false;

      // push consumer ahead to maintain order
      if (isFull)
        Increment(itConsumer);

      Increment(itProducer);

      isFull = (itProducer == itConsumer);
    }

    void IncrementConsumer()
    {
      isFull = false;

      Increment(itConsumer);

      isEmpty = (itProducer == itConsumer);
    }

    void Increment(typename std::vector<Buffer>::iterator &it)
    {
      if (next(it) == end(ring))
        it = begin(ring);
      else
        ++it;
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
  m_impl->CloseConsumer();
}

template<typename Buffer>
std::future<Buffer const &> SwitchBufferConsumer<Buffer>::Switch()
{
  return m_impl->SwitchConsumer();
}

template<typename Buffer>
SwitchBufferConsumer<Buffer>::SwitchBufferConsumer(std::shared_ptr<detail::SwitchBufferImpl<Buffer>> impl)
  : m_impl(std::move(impl))
{}


template<typename Buffer>
std::unique_ptr<SwitchBuffer<Buffer>> SwitchBuffer<Buffer>::Create(size_t count)
{
  return std::unique_ptr<SwitchBuffer<Buffer>>(new SwitchBuffer<Buffer>(count));
}

template<typename Buffer>
SwitchBuffer<Buffer>::~SwitchBuffer()
{}

template<typename Buffer>
std::unique_ptr<SwitchBufferProducer<Buffer>> SwitchBuffer<Buffer>::GetProducer()
{
  return std::unique_ptr<SwitchBufferProducer<Buffer>>(new SwitchBufferProducer<Buffer>(m_impl));
}

template<typename Buffer>
std::unique_ptr<SwitchBufferConsumer<Buffer>> SwitchBuffer<Buffer>::GetConsumer()
{
  return std::unique_ptr<SwitchBufferConsumer<Buffer>>(new SwitchBufferConsumer<Buffer>(m_impl));
}

template<typename Buffer>
SwitchBuffer<Buffer>::SwitchBuffer(size_t count)
  : m_impl(std::shared_ptr<detail::SwitchBufferImpl<Buffer>>(new detail::SwitchBufferImpl<Buffer>(count)))
{}

#endif // SWITCHBUFFER_IMPL_H
