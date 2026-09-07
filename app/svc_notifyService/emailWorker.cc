#include "emailWorker.h"
#include <bite_scaffold/log.h>

namespace notifyService {

EmailWorker::EmailWorker() : _running(false) {
}

EmailWorker::~EmailWorker() {
    stop();
}

void EmailWorker::start() {
    // 1. 检查线程是否已在运行（防止重复启动）
    if (_running.load()) {
        return;
    }
    // 2. 启动消费线程
    _running.store(true);
    _workerThread = std::thread(&EmailWorker::workerThread, this);
    INF("EmailWorker started");
}

void EmailWorker::stop() {
    // 1. 检查线程是否正在运行
    if (!_running.load()) {
        return;
    }
    // 2. 设置退出标志
    _running.store(false);
    // 3. 唤醒线程处理退出判断（若队列非空会先排空再退出）
    _cond.notify_one();
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
    INF("EmailWorker stopped");
}

void EmailWorker::addTask(EmailTask task) {
    // 1. 任务入队（锁内临界区尽量短）
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _taskQueue.push(std::move(task));
    }
    // 2. 通知消费线程有任务可处理
    _cond.notify_one();
}

void EmailWorker::workerThread() {
    while (true) {
        EmailTask task;
        // 1. 获取任务（锁内）
        {
            // 1.1 阻塞等待：队列有任务 或 收到退出信号
            std::unique_lock<std::mutex> lock(_mutex);
            _cond.wait(lock, [this] {
                return !_running.load() || !_taskQueue.empty();
            });
            // 1.2 退出条件：停止标志已置位且队列已排空
            if (!_running.load() && _taskQueue.empty()) {
                return;
            }
            // 1.3 取出任务
            task = std::move(_taskQueue.front());
            _taskQueue.pop();
        }
        // 2. 处理任务（锁外执行，发送耗时不会阻塞入队）
        if (task._emailSender) {
            task._emailSender->sendEmail(task._toEmail, task._subject, task._content);
        }
    }
}

} // namespace notifyService
