﻿#include "stdafx.h"
#include "tcpclient_linux.h"
#include "looper/handler.h"
#include "tcpserver_linux.h"
#include "net/dnslooper.h"
#include "../../core/looper/message.inl"
using namespace std;
using namespace Bear::Core;

namespace Bear {
namespace Core
{
namespace Net {

enum
{
	BM_DNS_ACK,
};

TcpClient_Linux::TcpClient_Linux()
{
	//DV("%s,this=0x%x", __func__, this);
	mSock = INVALID_SOCKET;
	mServerSide = true;
	mWaitFirstEvent = true;
	mListenWritable = false;
	SetObjectName("TcpClient_Linux");
}

TcpClient_Linux::~TcpClient_Linux()
{
	{
		//DV("%s,this=0x%x,name=%s,mSock=%d", __func__, this, GetObjectName().c_str(),mSock);
	}
	ASSERT(mSock == -1);
	SockTool::CLOSE_SOCKET(mSock);
	//Close();
}

int TcpClient_Linux::ConnectHelper(string ip)
{
	int port = mBundle.GetInt("port");

	SOCKET s = SockTool::SocketEx(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	SockTool::SetAsync(s);
	mSock = s;

	unsigned long handle = (unsigned long)(LONGLONG)GetLooperHandle();
	int ret = -1;
#ifdef __APPLE__
	struct kevent evt[2] = { 0 };
	//注意EVFILT_READ为-1,EVFILT_WRITE为-2,所以不能用|合并为一个EVT_SET
	EV_SET(&evt[0], s, EVFILT_WRITE, EV_ADD, 0, 0, (EpollProxy*)this);
	EV_SET(&evt[1], s, EVFILT_READ, EV_ADD, 0, 0, (EpollProxy*)this);

	ret = kevent(handle, evt, COUNT_OF(evt), NULL, 0, NULL);
	ASSERT(ret == 0);
#else
	struct epoll_event evt = { 0 };
	evt.events = EPOLLIN
		| EPOLLOUT	//如果加上会一直触发此事件
		| EPOLLRDHUP
		| EPOLLERR
		;

	evt.data.ptr = (EpollProxy*)this;
	ret = epoll_ctl((int)handle, EPOLL_CTL_ADD, (int)s, &evt);
#endif
	ASSERT(ret == 0);

	struct sockaddr_in servAddr;
	servAddr.sin_addr.s_addr = inet_addr(ip.c_str());
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	ret = connect(s, (struct sockaddr *) &servAddr, sizeof(struct sockaddr));

	if (ret && SockTool::IsWouldBlock())
	{
		ret = 0;
	}

#ifdef _CONFIG_ANDROID
	DV("connect ret=%d,error=%d(%s),sock=%d",ret,errno,strerror(errno),mSock);
#endif

	return ret;
}

int TcpClient_Linux::Connect(Bundle& info)
{
	if (mSock != INVALID_SOCKET)
	{
		ASSERT(FALSE);
		return -1;
	}

	//BindProcData(mAddress, "address");

	mBundle = info;
	mServerSide = false;
	mWaitFirstEvent = true;

	string addr = info.GetString("address");
	mAddress = addr;
	if (SockTool::IsValidIP(addr.c_str()))
	{
		return ConnectHelper(addr);
	}

	auto mainLooper = Looper::GetMainLooper();
	ASSERT(mainLooper);

	if (mainLooper)
	{
		auto dnsLooper = _MObject(DnsLooper,"DnsLooper");
		if (!dnsLooper)
		{
			auto looper = make_shared<DnsLooper>();
			mainLooper->AddChild(looper);
			looper->Start();

			dnsLooper = _MObject(DnsLooper, "DnsLooper");
		}

		if (dnsLooper)
		{
			dnsLooper->AddRequest(addr, shared_from_this(), BM_DNS_ACK);
		}
	}

	return 0;
}

int TcpClient_Linux::OnConnect(long handle, Bundle* extraInfo)
{
	SOCKET s = (SOCKET)handle;
	DV("%s,sock=%d", __func__, s);

	if (s != INVALID_SOCKET)
	{
		ASSERT(mSock == INVALID_SOCKET);
		mSock = s;
		{
			mPeerDesc = Core::StringTool::Format("%s:%d", SockTool::GetPeerIP(s).c_str(), SockTool::GetPeerPort(s));
			mLocalDesc = Core::StringTool::Format("%s:%d", SockTool::GetLocalIP(s).c_str(), SockTool::GetLocalPort(s));
		}

		unsigned long handle = (unsigned long)(LONGLONG)GetLooperHandle();
		int ret = -1;
#ifdef __APPLE__
		struct kevent evt = { 0 };
		EV_SET(&evt, s, EVFILT_READ, EV_ADD, 0, 0, (EpollProxy*)this);

		ret = kevent(handle, &evt, 1, NULL, 0, NULL);
#else
		struct epoll_event evt = { 0 };
		evt.events = EPOLLIN
			//| EPOLLOUT	//如果加上会一直触发此事件
			| EPOLLRDHUP
			| EPOLLERR
			;

		evt.data.ptr = (EpollProxy*)this;
		ret = epoll_ctl((int)handle, EPOLL_CTL_ADD, (int)s, &evt);
#endif
		DV("epoll_ctl,handle=%d,s=%d,ret=%d", handle, s, ret);
		//ASSERT(ret == 0);
		if (ret)
		{
			DW("handler bind event fail,error=%d(%s)",errno,strerror(errno));
		}
	}
	else
	{
		//active connect ack
		ASSERT(FALSE);
	}

	SignalOnConnect(this, 0, nullptr, extraInfo);

	return 0;
}

void TcpClient_Linux::Close()
{
	ASSERT(IsMyselfThread());

	bool needFireEvent = false;
	if (mSock != INVALID_SOCKET)
	{
		{
			unsigned long handle = (unsigned long)(LONGLONG)GetLooperHandle();
			int ret = -1;
#ifdef __APPLE__
			DW("todo?");
#else
			struct epoll_event evt = { 0 };
			ret = epoll_ctl((int)handle, EPOLL_CTL_DEL, (int)mSock, &evt);//remove all events
#endif

			if (ret)
			{
				DW("fail");
			}
		}

		shutdown(mSock, SD_BOTH);
		closesocket(mSock);
		mSock = INVALID_SOCKET;
		needFireEvent = true;
	}

	if (needFireEvent)
	{
		OnClose();
	}

	Destroy();
}

void TcpClient_Linux::OnReceive()
{
	//DV("%s", __func__);
	SignalOnReceive(this);
}

//返回成功提交的字节数
int TcpClient_Linux::Send(LPVOID data, int dataLen)
{
	//DV("%s,dataLen=%d", __func__,dataLen);

	ASSERT(IsMyselfThread());
	int ret = (int)send(mSock, (char*)data, dataLen, 0);
	if (ret > 0)
	{
		UpdateSendTick();
	}

	if (ret != dataLen)
	{
		int err = errno;
		bool again = (err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS || err == WSAEWOULDBLOCK);
		if (again)
		{

			static int idx = -1;
			++idx;
			if ((idx % 50) == 0)
			{
				DV("send only partial,err=%d(%s),this=%p", err, strerror(err), this);
			}

			EnableListenWritable();
		}
		else
		{
			if (err != 32)//broken pipe
			{
				if (ret <= 0)
				{
					DV("###send fail,this=%p,mSock=%d,data=0x%08x,dataLen=%4d,ret=%4d,error=%d(%s)", this, mSock, data, dataLen, ret, err, strerror(err));
				}
			}
		}
	}

	return ret;
}

LRESULT TcpClient_Linux::OnMessage(UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case BM_DESTROY:
	{
		//Close();
		break;
	}
	case BM_DUMP:
	{
		break;
	}
	case BM_DNS_ACK:
	{
		string dns = (const char*)wp;
		string ip = (const char *)lp;
		DV("dnsAck,%s=[%s]", dns.c_str(), ip.c_str());
		if (dns == mBundle.GetString("address") && mSock == INVALID_SOCKET)
		{
			ConnectHelper(ip);
		}
		else
		{
			//已取消连接
		}

		return 0;
	}
	}

	return __super::OnMessage(msg, wp, lp);
}

void TcpClient_Linux::OnClose()
{
	SignalOnClose(this);
}

//返回接收到的字节数
int TcpClient_Linux::Receive(LPVOID buf, int bufLen)
{
	//DV("%s",__func__);

	if (!buf || bufLen <= 0)
	{
		DW("buf=0x%08x,bufLen=%d", buf, bufLen);
		return -1;
	}

	int ret = (int)recv(mSock, (char*)buf, bufLen, 0);
	if (ret > 0)
	{
		UpdateRecvTick();
	}
	return ret;
}

//当可写时会调用本接口
void TcpClient_Linux::OnSend()
{
	//DV("%s", __func__);

	SignalOnSend(this);
}

void TcpClient_Linux::OnEvent(DWORD events)
{
	//DV("%s,mSock=%d,events=0x%02x",__func__,mSock,events);

	if (mWaitFirstEvent)
	{
		mWaitFirstEvent = false;

		int error = -1;
		socklen_t len = sizeof(error);
		getsockopt(mSock, SOL_SOCKET, SO_ERROR, (char *)&error, &len);
		if (error)
		{
			//128,Network is unreachable
			//134(Transport endpoint is not connected)
			if (error != 128 && error != 134)
			{
				DV("%s,addr=[%s],mSock=%d,events=0x%02x,peer(%s:%d)error=%d,%s",
					__func__
					, mAddress.c_str()
					, mSock
					, events
					, SockTool::GetPeerIP(mSock).c_str()
					, SockTool::GetPeerPort(mSock)
					, error
					, strerror(error)
				);
			}
		}

		if (!mServerSide)
		{
			/*
			if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				error = -1;
			}
			 //*/

			if (error == 0)
			{
				DisableListenWritable();
			}

			//客户端主动连接，第一次收到可写事件时，触发连接成功或失败事件
			SignalOnConnect(this, error, nullptr, nullptr);
			if (error)
			{
				return;
			}
		}
	}

	//注意hup可能和in事件一起返回，所以要先处理in事件才能确保接收完整的数据
	if (events & EPOLLIN)
	{
		OnReceive();
	}

	if (events & EPOLLOUT)
	{
		//DV("EPOLLOUT,this=%p",this);
		if (mListenWritable)
		{
			DisableListenWritable();
		}

		OnSend();
	}

	if (events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
	{
		Close();
		return;
	}

}

void TcpClient_Linux::EnableListenWritable()
{
	mListenWritable = true;

	SOCKET& s = mSock;
	unsigned long handle = (unsigned long)(LONGLONG)GetLooperHandle();
	int ret = -1;

#ifdef __APPLE__
	struct kevent evt = { 0 };
	EV_SET(&evt, s, EVFILT_WRITE, EV_ADD, 0, 0, (EpollProxy*)this);

	ret = kevent(handle, &evt, 1, NULL, 0, NULL);
#else
	struct epoll_event evt = { 0 };
	evt.events = EPOLLIN
		| EPOLLOUT	//如果加上会一直触发此事件
		| EPOLLRDHUP
		| EPOLLERR
		;

	evt.data.ptr = (EpollProxy*)this;
	ret = epoll_ctl((int)handle, EPOLL_CTL_MOD, (int)s, &evt);
	//DV("epoll_ctl,handle=%d,s=%d,ret=%d", handle, s, ret);
#endif
	ASSERT(ret == 0);
	if (ret)
	{
		DW("%s fail",__func__);
	}
}

void TcpClient_Linux::DisableListenWritable()
{
	mListenWritable = false;

	SOCKET& s = mSock;
	unsigned long handle = (unsigned long)(LONGLONG)GetLooperHandle();
	int ret = -1;

#ifdef __APPLE__
	struct kevent evt = { 0 };
	EV_SET(&evt, s, EVFILT_WRITE, EV_DISABLE, 0, 0, (EpollProxy*)this);

	ret = kevent(handle, &evt, 1, NULL, 0, NULL);
#else
	struct epoll_event evt = { 0 };
	evt.events = EPOLLIN
		//| EPOLLOUT	//如果加上会一直触发此事件
		| EPOLLRDHUP
		| EPOLLERR
		;

	evt.data.ptr = (EpollProxy*)this;
	ret = epoll_ctl((int)handle, EPOLL_CTL_MOD, (int)s, &evt);
#endif

	if (ret)
	{
		DW("epoll_ctl,handle=%d,s=%d,ret=%d,error=%d(%s)", handle, s, ret, errno, strerror(errno));
	}

}

void TcpClient_Linux::MarkEndOfRecv()
{
	shutdown(mSock, SD_RECEIVE);
}

void TcpClient_Linux::MarkEndOfSend()
{
	mMarkEndOfSend = true;
	shutdown(mSock, SD_SEND);
}

}
}
}