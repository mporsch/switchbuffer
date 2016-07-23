#ifndef SWITCHBUFFER_IMPL_H
#define SWITCHBUFFER_IMPL_H

#ifndef SWITCHBUFFER_H
# error Include this file via switchbuffer.h only
#endif

template<typename Buffer>
SwitchBuffer<Buffer> *SwitchBuffer<Buffer>::Create(size_t count)
{
  return new SwitchBuffer<Buffer>(count);
}

template<typename Buffer>
void SwitchBuffer<Buffer>::ReleaseProducer()
{
  std::lock_guard<std::mutex> lock(m_mtx);
  m_closedProducer = true;

  // if there is an open promise, break it
  if (m_promise) {
    delete m_promise;
    m_promise = nullptr;
  }

  if (m_closedConsumer)
    delete this;
}

template<typename Buffer>
void SwitchBuffer<Buffer>::ReleaseConsumer()
{
  std::lock_guard<std::mutex> lock(m_mtx);
  m_closedConsumer = true;

  if (m_closedProducer)
    delete this;
}

template<typename Buffer>
Buffer &SwitchBuffer<Buffer>::GetProducer()
{
  std::lock_guard<std::mutex> lock(m_mtx);

  if (!m_isFirst) {
    // if there is an open promise, fulfill it
    if (m_promise) {
      // immediately swap consumer and producer slot
      std::swap(m_slotProducer, m_slotConsumer);
      m_promise->set_value(m_slotConsumer);
      delete m_promise;
      m_promise = nullptr;
    } else {
      IncrementProducer();

      std::swap(*m_itProducer, m_slotProducer);
    }
  } else {
    m_isFirst = false;
  }

  return m_slotProducer;
}

template<typename Buffer>
std::future<Buffer const &> SwitchBuffer<Buffer>::GetConsumer()
{
  std::lock_guard<std::mutex> lock(m_mtx);

  if (m_isEmpty) {
    if (m_closedProducer) {
      // create a promise to be broken immediately
      return std::promise<Buffer const &>().get_future();
    } else {
      // create a promise to fulfill on next Production
      m_promise = new std::promise<Buffer const &>();
      return m_promise->get_future();
    }
  } else {
    IncrementConsumer();

    std::swap(*m_itConsumer, m_slotConsumer);

    std::promise<Buffer const &> p;
    p.set_value(m_slotConsumer);
    return p.get_future();
  }
}

template<typename Buffer>
SwitchBuffer<Buffer>::SwitchBuffer(size_t count)
  : m_ring(count - 2, Buffer())
  , m_itProducer(m_ring.begin())
  , m_itConsumer(m_ring.begin())
  , m_isFull(false)
  , m_isEmpty(true)
  , m_promise(nullptr)
  , m_isFirst(true)
  , m_closedProducer(false)
  , m_closedConsumer(false)
{
}

template<typename Buffer>
SwitchBuffer<Buffer>::~SwitchBuffer()
{
  // if there is an open promise, break it
  if (m_promise)
    delete m_promise;
}

template<typename Buffer>
void SwitchBuffer<Buffer>::IncrementProducer()
{
  m_isEmpty = false;

  // push consumer ahead to maintain order
  if (m_isFull)
    Increment(m_itConsumer);

  Increment(m_itProducer);

  m_isFull = (m_itProducer == m_itConsumer);
}

template<typename Buffer>
void SwitchBuffer<Buffer>::IncrementConsumer()
{
  m_isFull = false;

  Increment(m_itConsumer);

  m_isEmpty = (m_itProducer == m_itConsumer);
}

template<typename Buffer>
void SwitchBuffer<Buffer>::Increment(typename std::vector<Buffer>::iterator &it)
{
  if (next(it) == end(m_ring))
    it = begin(m_ring);
  else
    ++it;
}

#endif // SWITCHBUFFER_IMPL_H

