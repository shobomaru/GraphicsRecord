// SimpleMovie.cpp

#include <Windows.h>
#include <tchar.h>
#include <intrin.h>
#include <stdexcept>
#include <Shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Mfuuid.lib")

using namespace std;

//! \brief COM smart pointer
template<typename T>
class com_ptr
{
	static_assert(is_base_of<IUnknown, T>::value, "T is not a COM.");
	using this_type = com_ptr<T>;
	T* mPtr = nullptr;
public:
	com_ptr()					{}
	com_ptr(T* p)				{ mPtr = p; }
	~com_ptr()					{ release(); }
	void release()
	{
		if(mPtr) {
			auto t = mPtr;
			mPtr = nullptr;
			t->Release();
		}
	}
	T*& get()					{ return mPtr; }
	T* operator->() const		{ return mPtr; }
	this_type& operator=(T* p)
	{
		release();
		mPtr = p;
		return *this;
	 }
	this_type& operator=(this_type&) = delete;
};

void CHK(HRESULT hr)
{
	if(FAILED(hr))
		throw runtime_error("");
}

template<typename T>
void CHK(T* p)
{
	if(FAILED(hr))
		throw runtime_error("HRESULT failed.");
}

template<typename T>
void CHK(com_ptr<T>& p)
{
	if(p.get() == nullptr)
		throw runtime_error("Nullptr.");
}

//! \brief Movie writer
class movie_writer
{
	com_ptr<IStream> mComStream;
	com_ptr<IMFByteStream> mOutputStream;
	com_ptr<IMFAttributes> mOutputAttr;
	com_ptr<IMFSinkWriter> mSinkWriter;
	DWORD mStreamIndex;
	com_ptr<IMFMediaType> mOutputType;
	com_ptr<IMFMediaType> mInputType;
	unsigned int mFrameSize;

	com_ptr<IMFMediaBuffer> mBuffer;
	UINT64 mTotalTime = 0;
public:
	movie_writer(const TCHAR* path,
				unsigned int width,
				unsigned int height,
				unsigned int frameRate)
	{
		CHK(CoInitialize(nullptr));
		CHK(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
		//CHK(mComStream = SHCreateMemStream(nullptr, 0));
		//CHK(MFCreateMFByteStreamOnStream(mComStream.get(), &mOutputStream.get()));
		CHK(MFCreateAttributes(&mOutputAttr.get(), 10));
		CHK(mOutputAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
		CHK(mOutputAttr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
		CHK(MFCreateSinkWriterFromURL(path, nullptr, mOutputAttr.get(), &mSinkWriter.get()));
		CHK(MFCreateMediaType(&mOutputType.get()));
		CHK(mOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
		CHK(mOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
		CHK(mOutputType->SetUINT32(MF_MT_AVG_BITRATE, 1 * 1024 * 1024));
		CHK(mOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		CHK(MFSetAttributeSize(mOutputType.get(), MF_MT_FRAME_SIZE, width, height));
		CHK(MFSetAttributeRatio(mOutputType.get(), MF_MT_FRAME_RATE, frameRate, 1));
		CHK(MFSetAttributeRatio(mOutputType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
		CHK(mSinkWriter->AddStream(mOutputType.get(), &mStreamIndex));
		mOutputType.release();
		CHK(MFCreateMediaType(&mInputType.get()));
		CHK(mInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
		CHK(mInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
		CHK(mInputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
		CHK(MFSetAttributeSize(mInputType.get(), MF_MT_FRAME_SIZE, width, height));
		CHK(MFSetAttributeRatio(mInputType.get(), MF_MT_FRAME_RATE, frameRate, 1));
		CHK(MFSetAttributeRatio(mInputType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
		CHK(mSinkWriter->SetInputMediaType(mStreamIndex, mInputType.get(), nullptr));
		mInputType.release();
		mFrameSize = 4 * width * height;
		CHK(MFCreateMemoryBuffer(mFrameSize, &mBuffer.get()));
		CHK(mSinkWriter->BeginWriting());
	}
	~movie_writer()
	{
	}
	void write(const char* data, UINT64 duration)
	{
		mBuffer.release();
		CHK(MFCreateMemoryBuffer(mFrameSize, &mBuffer.get()));
		BYTE* destPtr;
		CHK(mBuffer->Lock(&destPtr, nullptr, nullptr));
		if (reinterpret_cast<INT_PTR>(destPtr) % 16 != 0)
			throw runtime_error("Alignment error.");
#if 0
		for(auto i = 0u; i < mFrameSize; i += 16)
		{
			auto d = _mm_lddqu_si128(reinterpret_cast<const __m128i*>(data + i));
			_mm_stream_si128(reinterpret_cast<__m128i*>(destPtr + i), d);
		}
		_mm_mfence();
#else
		memcpy(destPtr, data, mFrameSize);
#endif
		CHK(mBuffer->Unlock());
		CHK(mBuffer->SetCurrentLength(mFrameSize));
		
		com_ptr<IMFSample> sample;
		CHK(MFCreateSample(&sample.get()));
		CHK(sample->AddBuffer(mBuffer.get()));
		CHK(sample->SetSampleTime(mTotalTime));
		CHK(sample->SetSampleDuration(duration));
		mTotalTime += duration;
		CHK(mSinkWriter->WriteSample(mStreamIndex, sample.get()));
	}
	void finalize()
	{
		CHK(mSinkWriter->Flush(mStreamIndex));
		CHK(mSinkWriter->Finalize());
		mTotalTime = 0;
	}
};

int main(int argc, char**argv)
{
	{
		movie_writer mw(_T("hoge.mp4"), 640, 360, 30);
		char* data = new char[4 * 640 * 360];
		for (auto i = 0u; i < 7 * 30; ++i)
		{
			unsigned int pixel = (i * 5) % 256;
			unsigned int shift = 8 * (((i * 5) / 256) % 3);
			pixel <<= shift;
			for (int c = 0; c < 640 * 360; ++c)
			{
				*(reinterpret_cast<unsigned int*>(data) + c) = pixel;
			}
			mw.write(data, 333333);
		}
		delete[] data;
		mw.finalize();
	}
	CHK(MFShutdown());
	return 0;
}
