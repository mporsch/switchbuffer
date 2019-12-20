# switchbuffer
A generic Producer/Multi-Consumer communication buffer implemented as C++11 header-only library without external dependencies.

Designed for use where a push-based producer meets pull-based consumers, e.g. a pollable interface to a network stream.
The producer has non-blocking write access to the buffer while multiple clients can access the last N most recent entries without interfering with one another.

## Design goals
* The buffer slot type is given as template argument.
* The single Producer has non-blocking access to the buffer slots, independent of the state or number of Consumers.
* Multiple Consumers have potentially blocking access to the buffer slots, implemented via std::future, depending on the fill level.
* Multiple buffer slots stored as ring of user-defined size allow to compensate intermittent differences in Producer and Consumer performance without loss.
* A Consumer that is generally slower than the Producer may skip to the most recently produced buffer slot.
* Producer and Consumers are given separate interfaces to remove any room for mishandling (interface segregation principle).
* Interfaces are distributed via smart pointers to handle Producer and Consumer shutdown and final resource cleanup.
* Consumers may empty the remaining buffer slots after the Producer is gone.

## Build
Build test using CMake or `$ g++ -o switchbuffer_test switchbuffer_test.cpp -std=c++11 -lpthread`
