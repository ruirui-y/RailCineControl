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
		thread->setObjectName(QString::number(i));
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
    QMutexLocker locker(&mutex_);
	if (!started_) return;
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
	http_mgr = CreateQObject<HttpServerMgr>();
	PostTask(http_mgr.get(), [](HttpServerMgr* server)
		{
			qDebug() << "HttpServer Start " << QThread::currentThread()->objectName();
			server->Start(HTTP_LISTEN_PORT);
		});
}