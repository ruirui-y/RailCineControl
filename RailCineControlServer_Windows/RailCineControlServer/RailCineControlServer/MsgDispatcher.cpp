#include "MsgDispatcher.h"

void MsgDispatcher::RegisterRoute(ServerApi::MsgId msgId, MsgHandler handler)
{
    if (m_router.contains(msgId)) {
        qDebug() << u8"⚠️ [MsgDispatcher] 警告：重复注册消息路由 ID:" << msgId;
        return;
    }
    m_router.insert(msgId, handler);
}

void MsgDispatcher::Dispatch(std::shared_ptr<ClientSession> session, ServerApi::MsgId msgId, const ServerApi::PacketHeader& header, const QByteArray& body)
{
    auto it = m_router.find(msgId);
    if (it != m_router.end()) {
        // 找到了对应的处理函数，直接执行！
        it.value()(session, header, body);
    }
    else {
        qDebug() << u8"❌ [MsgDispatcher] 未知消息类型，无法路由丢弃，MsgId:" << msgId;
    }
}