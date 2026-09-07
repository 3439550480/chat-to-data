#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "emailTask.h"

namespace notifyService {

// 邮件发送工作线程：生产者-消费者模型的消费者端
class EmailWorker {
public:
    EmailWorker();
    ~EmailWorker();
    // 启动发送邮件线程
    void start();
    // 停止线程（先排空队列再退出）
    void stop();
    // 添加发送邮件任务（生产者入口）
    void addTask(EmailTask task);
private:
    // 发送邮件线程函数（消费者循环）
    void workerThread();
private:
    std::queue<EmailTask> _taskQueue;   // 邮件任务队列
    std::mutex _mutex;                  // 互斥锁：保证队列的线程安全访问
    std::condition_variable _cond;      // 条件变量：通知消费线程有任务/退出
    std::thread _workerThread;          // 消费线程对象
    std::atomic<bool> _running;         // 运行标志（原子变量，跨线程读写）
};

} // namespace notifyService
