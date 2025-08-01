A C++ library for submitting and executing tasks across a pool of worker threads
Executes jobs in priority order, ensuring higherpriority tasks run first
Returns a future for each task so results or exceptions can be handled asynchronously
Allows you to increase or decrease the number of worker threads at runtime
Supports preexecution task cancellation and safely coordinates threads with mutexes and a condition variable
