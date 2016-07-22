#ifndef SWITCHBUFFER_H
#define SWITCHBUFFER_H

#include <vector>
#include <mutex>
#include <future>

template<typename Buffer>
class SwitchBuffer
{
public:
  static SwitchBuffer *Create(size_t count);
  void ReleaseProducer();
  void ReleaseConsumer();

  std::future<Buffer const &> GetConsumer();
  Buffer &GetProducer();

private:
  SwitchBuffer(size_t count);
  ~SwitchBuffer();

  void increment(typename std::vector<Buffer>::iterator &it);

private:
  std::mutex m_mtx;
  std::vector<Buffer> m_ring;
  Buffer m_slotProducer;
  Buffer m_slotConsumer;
  typename std::vector<Buffer>::iterator m_itProducer;
  typename std::vector<Buffer>::iterator m_itConsumer;
  std::promise<Buffer const &> *m_promise;
  bool m_isFirst;
  bool m_closedProducer;
  bool m_closedConsumer;
};

#include "switchbuffer_impl.h"

#endif // SWITCHBUFFER_H

