#include "switchbuffer.h"

#include <thread>          // for std::thread
#include <atomic>          // for std::atomic
#include <csignal>         // for signal
#include <iostream>        // for std::cout
#include <random>          // for std::uniform_int_distribution

using namespace std;
using Buffer = vector<unsigned char>;

mutex printMutex;                                    ///< Mutex to unmangle cout output
atomic<bool> shouldStop(false);                      ///< Thread watch variable
default_random_engine generator;                     ///< Random generator
uniform_int_distribution<int> distribution(1, 100);  ///< Random distribution

/// Handler for Ctrl-C
void SignalHandler(int)
{
  lock_guard<mutex> lock(printMutex);
  cout << "Shutting down...\n";
  shouldStop = true;
}

/// Producer thread
void Producer(unique_ptr<SwitchBufferProducer<Buffer>> sbuf)
{
  unsigned char i = 0;

  while (!shouldStop)
  {
    // get a buffer to write to
    Buffer &buf = sbuf->Switch();

    // simulate some processing delay
    this_thread::sleep_for(chrono::milliseconds(distribution(generator)));

    // produce something
    Buffer tmp{++i, ++i, ++i};

    { // print
      lock_guard<mutex> lock(printMutex);
      cout << "Producing ";
      for (auto &&e : tmp)
      {
        cout << (int)e << ", ";
      }
      cout << endl;
    }

    buf = move(tmp);
  }

  lock_guard<mutex> lock(printMutex);
  cout << "Releasing Producer...\n";
}

/// Consumer thread
void Consumer(unique_ptr<SwitchBufferConsumer<Buffer>> sbuf)
try
{
  while (true)
  {
    // get a future for the next Buffer
    future<Buffer const &> future = sbuf->Switch();

    // wait for the Buffer to become available
    Buffer const &buf = future.get();

    // simulate some processing delay
    this_thread::sleep_for(chrono::milliseconds(distribution(generator)));

    { // print
      lock_guard<mutex> lock(printMutex);
      cout << "Consuming ";
      for (auto &&e : buf)
      {
         cout << (int)e << ", ";
      }
      cout << endl;
    }
  }

  lock_guard<mutex> lock(printMutex);
  cout << "Releasing Consumer...\n";
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
  thread consumer;
  {
    // create SwitchBuffer. Will be released by Producer and Consumer
    SwitchBuffer<Buffer> sbuf(5);

    producer = thread(Producer, sbuf.GetProducer());
    consumer = thread(Consumer, sbuf.GetConsumer());
  }

  // block until Producer and Consumer are done
  if (producer.joinable())
    producer.join();
  if (consumer.joinable())
    consumer.join();

  return EXIT_SUCCESS;
}

