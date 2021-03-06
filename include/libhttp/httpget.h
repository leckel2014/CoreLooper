﻿#pragma once

#include "net/simpleconnect.h"
namespace Bear {
namespace Core {
namespace Net {
namespace Http {


//XiongWanPing 2018.07.11
//支持简单的http get和chunked
class HTTP_EXPORT HttpGet :public SimpleConnect
{
	SUPER(SimpleConnect)
public:
	HttpGet();
	virtual ~HttpGet();

	//url可以为http url,比如网页或文件
	virtual int Execute(string url, string saveAsFilePath = "");
	sigslot::signal4<HttpGet*, string&, int, ByteBuffer&>	SignalHttpGetAck;
	
	//返回下载速度,单位:KB/S,仅在下载成功后有意义
	double GetSpeed()const
	{
		return mAckInfo.mSpeed;
	}
protected:
	void OnDestroy();

	void OnConnect(Channel *endPoint, long error, ByteBuffer *pBox, Bundle* extraInfo);
	void ParseInbox();
	void OnRecvHttpAckBody(LPVOID data, int dataLen);

	enum eHttpAckStatus
	{
		eHttpAckStatus_WaitHeader,
		eHttpAckStatus_ReceivingBody,
		eHttpAckStatus_Done,
	};
	void SwitchStatus(eHttpAckStatus status);

	struct tagHttpAckInfo
	{
		tagHttpAckInfo()
		{
			mFile = nullptr;
			Reset();
		}
		~tagHttpAckInfo()
		{
			if (mFile)
			{
				fclose(mFile);
				mFile = nullptr;
			}
		}

		void Reset()
		{
			mHttpAckCode = 0;
			mHttpAckStatus = eHttpAckStatus_WaitHeader;
			mContentLength = 0;
			mBodyReceivedBytes = 0;
			mAckBody.clear();

			if (mFile)
			{
				fclose(mFile);
				mFile = nullptr;
			}
		}

		int				mHttpAckCode;
		eHttpAckStatus	mHttpAckStatus;
		DWORD			mContentLength;
		ULONGLONG		mBodyReceivedBytes = 0;
		ByteBuffer		mAckBody;
		string			mSaveAsFilePath;
		FILE			*mFile;
		ULONGLONG		mStartTick=0;//用来计算下载速度
		bool			mChunked = false;//是否采用Transfer-Encoding: chunked
		double			mSpeed=0.0f;//KB/秒,仅在下载成功后有效
	}mAckInfo;

	string	mUrl;
	bool mSignaled = false;

	struct tagHttpReqInfo
	{
		string	mHost;
		int		mPort;
		string	mPageUrl;

	}mReqInfo;

};

}
}
}
}
