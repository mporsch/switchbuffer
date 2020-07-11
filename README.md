# switchbuffer
A generic producer/multi-consumer communication buffer implemented as C++11 header-only library without external dependencies.

Designed for use where a push-based producer meets pull-based consumers, e.g. a pollable interface to a network stream.
The producer has non-blocking write access to the buffer while multiple consumers can access the last N most recent entries safely without interfering with one another or the producer.

![ring](https://user-images.githubusercontent.com/1180665/87229148-0be89200-c3a6-11ea-8e96-fc935d15fc76.png)

## Design goals
* The buffer slot type is given as template argument.
* The single producer has non-blocking access to the buffer slots, independent of the state or number of consumers.
* Multiple consumers can read the written buffer slots in parallel.
* Multiple buffer slots stored as ring of user-defined size allow to compensate intermittent differences in producer and consumer performance without loss.
* A consumer that is generally slower than the producer may skip to the most recently produced buffer slot.
* If a consumer has read all buffer slots, the returned std::future allows waiting for fresh input from the producer.
* Producer and consumers are given separate interfaces to remove any room for mishandling (interface segregation principle).
* Interfaces are distributed via smart pointers to handle producer and consumer shutdown and final resource cleanup.
* Consumers may empty the remaining buffer slots after the producer is gone.

## Build
Build test using CMake or `$ g++ -o switchbuffer_test switchbuffer_test.cpp -std=c++11 -lpthread`
