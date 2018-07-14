#include "switchbuffer.h"

#include <array>           // for std::array
#include <atomic>          // for std::atomic
#include <csignal>         // for signal
#include <iomanip>         // for std::setfill
#include <iostream>        // for std::cout
#include <map>             // for std::map
#include <random>          // for std::uniform_int_distribution
#include <thread>          // for std::thread

#define CONSUMER_COUNT 3
#define PRINT_LINES 30

using namespace std;
using Buffer = unsigned int;
struct Status : public std::array<char, CONSUMER_COUNT>
{
  Status()
  {
    this->fill(' ');
  }
};

mutex printMutex;                                     ///< Mutex to unmangle cout output
map<Buffer, Status> statusMap;                        ///< Status of Produce-Consume
atomic<bool> shouldStop(false);                       ///< Thread watch variable
default_random_engine generator;                      ///< Random generator
uniform_int_distribution<int> distribution(1, 1000);  ///< Random distribution

/// Handler for Ctrl-C
void SignalHandler(int)
{
  lock_guard<mutex> lock(printMutex);
  cout << "Shutting down...\n";
  shouldStop = true;
}

void ClearTerminal()
{
  // CSI[2J clears screen, CSI[H moves the cursor to top-left corner
  cout << "\x1B[2J\x1B[H";
}

void PrintStatus()
{
  ClearTerminal();
  for (auto &&p : statusMap) {
    cout << setw(3) << setfill(' ') << p.first << ": ";
    for (auto &&consumerStatus : p.second)
      cout << "|" << consumerStatus;
    cout << "|\n";
  }
}

/// Producer thread
void Producer(unique_ptr<SwitchBufferProducer<Buffer>> sbuf)
{
  Buffer i{};

  while (!shouldStop)
  {
    // get a buffer to write to
    Buffer &buf = sbuf->Switch();

    // produce something
    buf = i++;
    i %= PRINT_LINES;

    { // print
      lock_guard<mutex> lock(printMutex);
      statusMap[buf] = Status{};
      PrintStatus();
    }

    // simulate some processing delay
    this_thread::sleep_for(chrono::milliseconds(distribution(generator)));
  }

  lock_guard<mutex> lock(printMutex);
  cout << "Releasing Producer...\n";
}

/// Consumer thread
void Consumer(size_t threadId, unique_ptr<SwitchBufferConsumer<Buffer>> sbuf)
try
{
  while (true)
  {
    // get a future for the next Buffer
    future<Buffer const &> future = sbuf->Switch();

    // check if Buffer is available immediately
    bool wasDelayed = (future.wait_for(chrono::seconds(0)) == future_status::timeout);

    // wait for the Buffer to become available
    Buffer const &buf = future.get();

    { // print
      lock_guard<mutex> lock(printMutex);
      auto &&status = statusMap[buf];
      status[threadId] = (wasDelayed ? 'd' : 'x');
      PrintStatus();
    }

    // simulate some processing delay
    this_thread::sleep_for(chrono::milliseconds(distribution(generator)));
  }

  lock_guard<mutex> lock(printMutex);
  cout << this_thread::get_id() << ": Releasing Consumer " << threadId << "...\n";
}
catch(future_error const &)
{
  lock_guard<mutex> lock(printMutex);
  cout << "Producer has left. Releasing Consumer...\n";
}

int main(int, char **)
{
  // set signal handler (Ctrl-C)
  signal(SIGINT, SignalHandler);

  // start Producer and Consumer threads
  thread producer;
  thread consumers[CONSUMER_COUNT];
  {
    // create SwitchBuffer. Will be released by Producer and Consumers
    SwitchBuffer<Buffer> sbuf(5);

    producer = thread(Producer, sbuf.GetProducer());
    for (size_t i = 0; i < extent<decltype(consumers)>::value; ++i)
      consumers[i] = thread(Consumer, i, sbuf.GetConsumer());
  }

  // block until Producer and Consumers are done
  if (producer.joinable())
    producer.join();
  for (auto &&consumer : consumers)
    if (consumer.joinable())
      consumer.join();

  return EXIT_SUCCESS;
}
