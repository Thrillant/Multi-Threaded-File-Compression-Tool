#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

template <typename T>
class ThreadSafeQueue
{
private:
    queue<T> q;
    mutex mtx;
    condition_variable cv;
    bool is_closed = false;

public:
    // Pushes a new chunk into the queue
    void push(T item)
    {
        unique_lock<mutex> lock(mtx); // Lock the queue so no other thread can touch it
        q.push(move(item));           // Add the chunk
        cv.notify_one();              // Instantly wake up ONE sleeping worker thread
    }

    // Pulls a chunk out. Returns false if the queue is empty AND the file is fully read.
    bool pop(T &item)
    {
        unique_lock<mutex> lock(mtx);

        // If the queue is empty, the thread goes to sleep here.
        // It wakes up ONLY when data is pushed, or the queue is explicitly closed.
        cv.wait(lock, [this]()
                { return !q.empty() || is_closed; });

        // If the reader is done and the queue is totally empty, tell the worker to shut down.
        if (q.empty() && is_closed)
        {
            return false;
        }

        // Grab the chunk and remove it from the queue
        item = move(q.front());
        q.pop();
        return true;
    }

    // Closes the queue when the main thread finishes reading the input file
    void close()
    {
        unique_lock<mutex> lock(mtx);
        is_closed = true;
        cv.notify_all(); // Wake up ALL sleeping threads so they can exit the while loop gracefully
    }
};