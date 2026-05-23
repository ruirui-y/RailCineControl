#include "ThreadPool.h"
#include <QMutexLocker>
#include <QEventLoop>

#define HTTP_LISTEN_PORT												8182

ThreadPool::ThreadPool(QObject *parent)
	: QObject(parent)
{}

ThreadPool::~ThreadPool()
{
	Stop();
}

void ThreadPool::Start(size_t threadNum)
{
	{
		QMutexLocker locker(&mutex_);
		if (started_) return;
	}

	if (threadNum == 0) threadNum = QThread::idealThreadCount();
	for (size_t i = 0; i < threadNum; ++i)
	{
		QSharedPointer<WorkerThread> thread = QSharedPointer<WorkerThread>(new WorkerThread());

		QEventLoop loop;
		QObject::connect(thread.get(), &WorkerThread::SigReady, &loop, &QEventLoop::quit, Qt::QueuedConnection);

		thread->start();
		thread->setObjectName("Worker-Thread-" + QString::number(i));
		loop.exec();																									// 等到 worker run() 里 emit Ready()

		{
			QMutexLocker locker(&mutex_);
			threads_.push_back(thread); // 仅在修改容器时加锁
		}
	}

	{
		QMutexLocker locker(&mutex_);
		started_ = true;
	}

	InitNetwork();
}

void ThreadPool::Stop()
{
	qDebug() << "ThreadPool::Stop()";
	QMutexLocker locker(&mutex_);
	if (!started_) return;

	// 1. 显式停止 HTTP 监听，让专属线程从 listen 阻塞中退出来
	if (http_mgr) {
		http_mgr->Stop();
	}
	http_mgr.reset(); // 安全销毁

	// 2. 清理并关闭 HTTP 专属网络线程
	if (http_thread_) {
		http_thread_->quit();
		http_thread_->wait();
		http_thread_.reset();
	}

	// 3. 正常清理并关闭业务大锅饭线程池
	for (auto& thread : threads_)
	{
		thread->quit();
		thread->wait();
	}
	threads_.clear();
	started_ = false;
}

QSharedPointer<WorkerThread> ThreadPool::GetThread()
{
    QMutexLocker locker(&mutex_);
	if (!started_) return nullptr;
	if (threads_.empty()) return nullptr;

	QSharedPointer<WorkerThread> thread = threads_[currentThreadIndex_];
	currentThreadIndex_ = (currentThreadIndex_ + 1) % threads_.size();

	return thread;
}

void ThreadPool::PostQueryTask(const QString& sql, QueryCallback cb, bool bIsAsync, const QList<QVariant>& params)
{
	// 按值捕获参数，交给分发引擎，在子线程解包调用！
	DispatchToWorker([=](WorkerThread* worker) {
		worker->SlotPostSqlQueryTask(sql, cb, bIsAsync, params);
		});
}

void ThreadPool::PostUpdateTask(const QString& sql, UpdateCallback cb, bool bIsAsync, const QList<QVariant>& params)
{
	DispatchToWorker([=](WorkerThread* worker) {
		worker->SlotPostSqlUpdateTask(sql, cb, bIsAsync, params);
		});
}

void ThreadPool::PostTransactionTask(const QList<QString>& sqls, TransactionCallback cb, bool bIsAsync, const QVariantList& allParams)
{
	DispatchToWorker([=](WorkerThread* worker) {
		worker->SlotPostSqlTransactionTask(sqls, cb, bIsAsync, allParams);
		});
}

void ThreadPool::InitNetwork()
{
	// 1. 单独启动一个专门用于 HTTP 监听的线程，坚决不放入 threads_ 数组！
	http_thread_ = QSharedPointer<WorkerThread>(new WorkerThread());

	QEventLoop loop;
	QObject::connect(http_thread_.get(), &WorkerThread::SigReady, &loop, &QEventLoop::quit, Qt::QueuedConnection);

	http_thread_->start();
	http_thread_->setObjectName("HttpNetworkThread");
	loop.exec();																											// 等待专属线程就绪

	// 2. 直接调用该专属线程的方法创建对象，确保对象依附在 HttpNetworkThread 上
	http_mgr = http_thread_->CreateQObject<HttpServerMgr, QSharedPointer>();

	// 3. 跨线程投递 Start 任务，只在这个专属线程里执行死循环
	QMetaObject::invokeMethod(http_mgr.get(), [this]() 
		{
			qDebug() << u8"HttpServer 专属网络线程启动: " << QThread::currentThread()->objectName();
			// 动态读取全局配置的端口号并启动
			http_mgr->Start(GlobalConfig::Instance()->GetHttpPort());
		}, Qt::QueuedConnection);
}