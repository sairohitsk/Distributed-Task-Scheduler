#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <unordered_set>

struct Task {
    std::function<void()> func;
    int taskId;
    int priority;
};

struct Result {
    int taskId;
    std::string output;
    std::exception_ptr exception;
};

struct CompareTask {
    bool operator()(const Task& t1, const Task& t2) {
        return t1.priority < t2.priority;
    }
};

class TaskScheduler {
public:
    TaskScheduler(size_t numThreads);
    ~TaskScheduler();
    void submitTask(const Task& task);
    std::future<Result> submitTaskWithResult(const Task& task);
    void scaleThreads(size_t numThreads);
    std::vector<Result> getResults();
    void cancelTask(int taskId);

private:
    void workerThread();

    std::vector<std::thread> workers;
    std::priority_queue<Task, std::vector<Task>, CompareTask> taskQueue;
    std::unordered_map<int, std::promise<Result>> promises;
    std::unordered_set<int> canceledTasks;
    std::vector<Result> results;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::atomic<bool> stop;
    std::atomic<size_t> activeThreads;
};

TaskScheduler::TaskScheduler(size_t numThreads) : stop(false), activeThreads(0) {
    scaleThreads(numThreads);
}

TaskScheduler::~TaskScheduler() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stop = true;
    }
    cv.notify_all();
    for (std::thread &worker : workers) {
        worker.join();
    }
}

void TaskScheduler::submitTask(const Task& task) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        taskQueue.push(task);
    }
    cv.notify_one();
}

std::future<Result> TaskScheduler::submitTaskWithResult(const Task& task) {
    std::promise<Result> promise;
    auto future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        promises[task.taskId] = std::move(promise);
        taskQueue.push(task);
    }
    cv.notify_one();
    return future;
}

void TaskScheduler::scaleThreads(size_t numThreads) {
    std::lock_guard<std::mutex> lock(queueMutex);
    while (workers.size() < numThreads) {
        workers.emplace_back(&TaskScheduler::workerThread, this);
    }
    while (workers.size() > numThreads) {
        workers.back().detach();
        workers.pop_back();
    }
}

std::vector<Result> TaskScheduler::getResults() {
    std::lock_guard<std::mutex> lock(queueMutex);
    return results;
}

void TaskScheduler::cancelTask(int taskId) {
    std::lock_guard<std::mutex> lock(queueMutex);
    canceledTasks.insert(taskId);
}

void TaskScheduler::workerThread() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [this] { return stop || !taskQueue.empty(); });
            if (stop && taskQueue.empty())
                return;
            task = taskQueue.top();
            taskQueue.pop();
            ++activeThreads;
        }

        if (canceledTasks.find(task.taskId) != canceledTasks.end()) {
            // Task was canceled, continue to next iteration
            std::lock_guard<std::mutex> lock(queueMutex);
            if (promises.find(task.taskId) != promises.end()) {
                promises[task.taskId].set_value(Result{task.taskId, "Canceled", nullptr});
                promises.erase(task.taskId);
            }
            --activeThreads;
            continue;
        }

        Result result{task.taskId, "", nullptr};
        try {
            task.func();
            result.output = "Completed";
        } catch (...) {
            result.exception = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            results.push_back(result);
            if (promises.find(task.taskId) != promises.end()) {
                promises[task.taskId].set_value(result);
                promises.erase(task.taskId);
            }
            --activeThreads;
        }
    }
}

int main() {
    TaskScheduler scheduler(4);

    std::vector<std::future<Result>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(scheduler.submitTaskWithResult({[i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));
            std::cout << "Task " << i << " executed." << std::endl;
        }, i, i % 5}));
    }

    scheduler.scaleThreads(6);

    // Cancel a task for demonstration
    scheduler.cancelTask(3);

    for (auto& future : futures) {
        Result result = future.get();
        if (result.exception) {
            try {
                std::rethrow_exception(result.exception);
            } catch (const std::exception& e) {
                std::cerr << "Task " << result.taskId << " failed with exception: " << e.what() << std::endl;
            }
        } else {
            std::cout << "Task " << result.taskId << " result: " << result.output << std::endl;
        }
    }

    return 0;
}
