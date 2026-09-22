#include "data/services/CameraCaptureService.h"

#include <QDateTime>
#include <QMetaObject>
#include <QPointer>

#ifdef Q_OS_WIN
#include <windows.h>

#include <dshow.h>
#include <qedit.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>
#endif

namespace rov
{

#ifdef Q_OS_WIN
namespace
{

const CLSID kSampleGrabberClass = {0xc1f400a0,
                                   0x3f08,
                                   0x11d3,
                                   {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const CLSID kNullRendererClass = {0xc1f400a4,
                                  0x3f08,
                                  0x11d3,
                                  {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};

template <typename T>
void releaseCom(T *&object)
{
    if (object != nullptr)
    {
        object->Release();
        object = nullptr;
    }
}

void freeMediaType(AM_MEDIA_TYPE &mediaType)
{
    if (mediaType.cbFormat != 0 && mediaType.pbFormat != nullptr)
    {
        CoTaskMemFree(mediaType.pbFormat);
        mediaType.pbFormat = nullptr;
        mediaType.cbFormat = 0;
    }
    releaseCom(mediaType.pUnk);
}

QString hresultMessage(HRESULT result)
{
    wchar_t *buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD size = FormatMessageW(flags, nullptr, static_cast<DWORD>(result), 0,
                                      reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    const QString message = size > 0 ? QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed()
                                     : QStringLiteral("未知错误");
    if (buffer != nullptr)
        LocalFree(buffer);
    return QStringLiteral("0x%1（%2）")
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'))
        .arg(message);
}

struct CameraDevice
{
    QString name;
    IMoniker *moniker = nullptr;
};

void releaseDevices(std::vector<CameraDevice> &devices)
{
    for (CameraDevice &device : devices)
        releaseCom(device.moniker);
    devices.clear();
}

HRESULT enumerateDevices(std::vector<CameraDevice> &devices)
{
    ICreateDevEnum *deviceEnumerator = nullptr;
    IEnumMoniker *monikerEnumerator = nullptr;
    HRESULT result = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&deviceEnumerator));
    if (FAILED(result))
        return result;

    result = deviceEnumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
                                                      &monikerEnumerator, 0);
    if (result == S_FALSE)
    {
        releaseCom(deviceEnumerator);
        return S_OK;
    }
    if (FAILED(result))
    {
        releaseCom(deviceEnumerator);
        return result;
    }

    IMoniker *moniker = nullptr;
    ULONG fetched = 0;
    while (monikerEnumerator->Next(1, &moniker, &fetched) == S_OK)
    {
        QString name = QStringLiteral("未命名相机");
        IPropertyBag *properties = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&properties))))
        {
            VARIANT value;
            VariantInit(&value);
            if (SUCCEEDED(properties->Read(L"FriendlyName", &value, nullptr)) &&
                value.vt == VT_BSTR && value.bstrVal != nullptr)
                name = QString::fromWCharArray(value.bstrVal);
            VariantClear(&value);
        }
        releaseCom(properties);
        devices.push_back({name, moniker});
        moniker = nullptr;
    }

    releaseCom(moniker);
    releaseCom(monikerEnumerator);
    releaseCom(deviceEnumerator);
    return S_OK;
}

struct VideoFormat
{
    int width = 0;
    int height = 0;
    int stride = 0;
    bool topDown = false;
};

class FrameCallback final : public ISampleGrabberCB
{
  public:
    explicit FrameCallback(CameraCaptureService *receiver) : m_receiver(receiver) {}

    void setFormat(const VideoFormat &format)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_format = format;
    }

    STDMETHODIMP QueryInterface(REFIID interfaceId, void **object) override
    {
        if (object == nullptr)
            return E_POINTER;
        if (interfaceId == IID_IUnknown || interfaceId == IID_ISampleGrabberCB)
        {
            *object = static_cast<ISampleGrabberCB *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_referenceCount));
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&m_referenceCount));
        if (count == 0)
            delete this;
        return count;
    }

    STDMETHODIMP SampleCB(double, IMediaSample *) override { return E_NOTIMPL; }

    STDMETHODIMP BufferCB(double, BYTE *buffer, long bufferLength) override
    {
        VideoFormat format;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            format = m_format;
        }
        if (buffer == nullptr || format.width <= 0 || format.height <= 0 ||
            bufferLength < format.stride * format.height)
            return S_OK;

        QImage image(format.width, format.height, QImage::Format_RGB888);
        for (int y = 0; y < format.height; ++y)
        {
            const int sourceY = format.topDown ? y : format.height - 1 - y;
            const BYTE *source = buffer + sourceY * format.stride;
            uchar *destination = image.scanLine(y);
            for (int x = 0; x < format.width; ++x)
            {
                destination[x * 3] = source[x * 3 + 2];
                destination[x * 3 + 1] = source[x * 3 + 1];
                destination[x * 3 + 2] = source[x * 3];
            }
        }

        if (!m_receiver.isNull())
        {
            QMetaObject::invokeMethod(m_receiver.data(), "acceptFrame", Qt::QueuedConnection,
                                      Q_ARG(QImage, image));
        }
        return S_OK;
    }

  private:
    ~FrameCallback() = default;

    LONG m_referenceCount = 1;
    QPointer<CameraCaptureService> m_receiver;
    std::mutex m_mutex;
    VideoFormat m_format;
};

} // namespace
#endif

class CameraCaptureService::Impl
{
  public:
    explicit Impl(CameraCaptureService *owner) : m_owner(owner)
    {
#ifdef Q_OS_WIN
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_uninitializeCom = SUCCEEDED(result);
#else
        Q_UNUSED(m_owner)
#endif
    }

    ~Impl()
    {
        close();
#ifdef Q_OS_WIN
        if (m_uninitializeCom)
            CoUninitialize();
#endif
    }

    QStringList deviceNames(QString *error)
    {
        QStringList names;
#ifdef Q_OS_WIN
        std::vector<CameraDevice> devices;
        const HRESULT result = enumerateDevices(devices);
        if (FAILED(result))
        {
            if (error != nullptr)
                *error = QStringLiteral("枚举相机失败：%1").arg(hresultMessage(result));
            return names;
        }
        for (const CameraDevice &device : devices)
            names.append(device.name);
        releaseDevices(devices);
#else
        if (error != nullptr)
            *error = QStringLiteral("当前平台不支持 DirectShow 相机采集");
#endif
        return names;
    }

    bool open(int deviceIndex, QString &deviceName, VideoFormat &format, QString *error)
    {
#ifdef Q_OS_WIN
        close();
        std::vector<CameraDevice> devices;
        HRESULT result = enumerateDevices(devices);
        if (FAILED(result))
        {
            if (error != nullptr)
                *error = QStringLiteral("枚举相机失败：%1").arg(hresultMessage(result));
            return false;
        }
        if (deviceIndex < 0 || deviceIndex >= static_cast<int>(devices.size()))
        {
            if (error != nullptr)
                *error = QStringLiteral("相机序号无效，请先刷新设备列表");
            releaseDevices(devices);
            return false;
        }
        deviceName = devices[static_cast<std::size_t>(deviceIndex)].name;

        result = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&m_graph));
        if (SUCCEEDED(result))
            result = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_captureBuilder));
        if (SUCCEEDED(result))
            result = m_captureBuilder->SetFiltergraph(m_graph);
        if (SUCCEEDED(result))
            result = devices[static_cast<std::size_t>(deviceIndex)].moniker->BindToObject(
                nullptr, nullptr, IID_PPV_ARGS(&m_source));
        releaseDevices(devices);
        if (SUCCEEDED(result))
            result = m_graph->AddFilter(m_source, L"Camera Source");
        if (SUCCEEDED(result))
            result = CoCreateInstance(kSampleGrabberClass, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_sampleGrabberFilter));
        if (SUCCEEDED(result))
            result = m_sampleGrabberFilter->QueryInterface(IID_PPV_ARGS(&m_sampleGrabber));

        AM_MEDIA_TYPE requestedType{};
        requestedType.majortype = MEDIATYPE_Video;
        requestedType.subtype = MEDIASUBTYPE_RGB24;
        requestedType.formattype = FORMAT_VideoInfo;
        if (SUCCEEDED(result))
            result = m_sampleGrabber->SetMediaType(&requestedType);
        if (SUCCEEDED(result))
            result = m_sampleGrabber->SetOneShot(FALSE);
        if (SUCCEEDED(result))
            result = m_sampleGrabber->SetBufferSamples(FALSE);
        if (SUCCEEDED(result))
            result = m_graph->AddFilter(m_sampleGrabberFilter, L"Frame Grabber");
        if (SUCCEEDED(result))
            result = CoCreateInstance(kNullRendererClass, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&m_nullRenderer));
        if (SUCCEEDED(result))
            result = m_graph->AddFilter(m_nullRenderer, L"Null Renderer");
        if (SUCCEEDED(result))
        {
            result = m_captureBuilder->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video,
                                                    m_source, m_sampleGrabberFilter,
                                                    m_nullRenderer);
            if (FAILED(result))
                result = m_captureBuilder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video,
                                                        m_source, m_sampleGrabberFilter,
                                                        m_nullRenderer);
        }

        if (SUCCEEDED(result))
        {
            AM_MEDIA_TYPE connectedType{};
            result = m_sampleGrabber->GetConnectedMediaType(&connectedType);
            if (SUCCEEDED(result) && connectedType.formattype == FORMAT_VideoInfo &&
                connectedType.pbFormat != nullptr &&
                connectedType.cbFormat >= sizeof(VIDEOINFOHEADER))
            {
                const auto *videoInfo =
                    reinterpret_cast<const VIDEOINFOHEADER *>(connectedType.pbFormat);
                format.width = videoInfo->bmiHeader.biWidth;
                format.height = std::abs(videoInfo->bmiHeader.biHeight);
                format.topDown = videoInfo->bmiHeader.biHeight < 0;
                format.stride = ((format.width * 24 + 31) / 32) * 4;
            }
            else if (SUCCEEDED(result))
            {
                result = VFW_E_INVALIDMEDIATYPE;
            }
            freeMediaType(connectedType);
        }

        if (SUCCEEDED(result))
        {
            m_callback = new FrameCallback(m_owner);
            m_callback->setFormat(format);
            result = m_sampleGrabber->SetCallback(m_callback, 1);
        }
        if (SUCCEEDED(result))
            result = m_graph->QueryInterface(IID_PPV_ARGS(&m_mediaControl));
        if (SUCCEEDED(result))
            result = m_mediaControl->Run();

        if (FAILED(result))
        {
            if (error != nullptr)
                *error = QStringLiteral("打开相机失败：%1").arg(hresultMessage(result));
            close();
            return false;
        }
        return true;
#else
        Q_UNUSED(deviceIndex)
        Q_UNUSED(deviceName)
        Q_UNUSED(format)
        if (error != nullptr)
            *error = QStringLiteral("当前平台不支持 DirectShow 相机采集");
        return false;
#endif
    }

    void close()
    {
#ifdef Q_OS_WIN
        if (m_mediaControl != nullptr)
            m_mediaControl->Stop();
        if (m_sampleGrabber != nullptr)
            m_sampleGrabber->SetCallback(nullptr, 0);
        releaseCom(m_callback);
        releaseCom(m_mediaControl);
        releaseCom(m_nullRenderer);
        releaseCom(m_sampleGrabber);
        releaseCom(m_sampleGrabberFilter);
        releaseCom(m_source);
        releaseCom(m_captureBuilder);
        releaseCom(m_graph);
#endif
    }

  private:
    CameraCaptureService *m_owner = nullptr;
#ifdef Q_OS_WIN
    bool m_uninitializeCom = false;
    IGraphBuilder *m_graph = nullptr;
    ICaptureGraphBuilder2 *m_captureBuilder = nullptr;
    IBaseFilter *m_source = nullptr;
    IBaseFilter *m_sampleGrabberFilter = nullptr;
    IBaseFilter *m_nullRenderer = nullptr;
    ISampleGrabber *m_sampleGrabber = nullptr;
    IMediaControl *m_mediaControl = nullptr;
    FrameCallback *m_callback = nullptr;
#endif
};

CameraCaptureService::CameraCaptureService(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this))
{
    m_snapshot.cameraDevice = QStringLiteral("--");
    m_snapshot.resolution = QStringLiteral("--");
    m_snapshot.frameRate = QStringLiteral("--");
    m_snapshot.pixelFormat = QStringLiteral("RGB24");
    m_snapshot.streamState = QStringLiteral("未启动");
    m_snapshot.lastFrame = QStringLiteral("--");
    m_snapshot.latency = QStringLiteral("本机采集");
    m_snapshot.nodeState = QStringLiteral("Windows DirectShow");
    m_snapshot.processingMode = QStringLiteral("原始画面");
    m_snapshot.modelsLoaded = QStringLiteral("0");
}

CameraCaptureService::~CameraCaptureService()
{
    m_impl->close();
}

void CameraCaptureService::refreshDevices()
{
    QString error;
    const QStringList names = m_impl->deviceNames(&error);
    emit devicesChanged(names);
    if (!m_snapshot.connected)
    {
        m_snapshot.cameraDevice = names.isEmpty() ? QStringLiteral("--") : names.first();
        m_snapshot.resolution = QStringLiteral("--");
        m_snapshot.frameRate = QStringLiteral("--");
        m_snapshot.pixelFormat = QStringLiteral("RGB24");
        m_snapshot.streamState = QStringLiteral("未启动");
        m_snapshot.lastFrame = QStringLiteral("--");
        m_snapshot.latency = QStringLiteral("本机采集");
        m_snapshot.nodeState = QStringLiteral("Windows DirectShow");
        m_snapshot.processingMode = QStringLiteral("原始画面");
        m_snapshot.modelsLoaded = QStringLiteral("0");
        emit snapshotChanged(m_snapshot);
    }
    if (!error.isEmpty())
        emit errorOccurred(error);
    else if (names.isEmpty())
        emit errorOccurred(QStringLiteral("未发现可用相机，请检查连接和 Windows 相机权限"));
}

void CameraCaptureService::startCamera(int deviceIndex)
{
    QString deviceName;
    VideoFormat format;
    QString error;
    if (!m_impl->open(deviceIndex, deviceName, format, &error))
    {
        m_snapshot.connected = false;
        m_snapshot.streamState = QStringLiteral("打开失败");
        emit snapshotChanged(m_snapshot);
        emit errorOccurred(error);
        return;
    }

    m_fpsWindowStartMs = QDateTime::currentMSecsSinceEpoch();
    m_fpsWindowFrames = 0;
    m_snapshot.connected = true;
    m_snapshot.cameraDevice = deviceName;
    m_snapshot.resolution = QStringLiteral("%1 × %2").arg(format.width).arg(format.height);
    m_snapshot.frameRate = QStringLiteral("计算中");
    m_snapshot.pixelFormat = QStringLiteral("RGB24");
    m_snapshot.streamState = QStringLiteral("采集中");
    m_snapshot.lastFrame = QStringLiteral("等待首帧");
    emit snapshotChanged(m_snapshot);
}

void CameraCaptureService::stopCamera()
{
    m_impl->close();
    m_snapshot.connected = false;
    m_snapshot.streamState = QStringLiteral("已停止");
    m_snapshot.frameRate = QStringLiteral("--");
    emit snapshotChanged(m_snapshot);
}

void CameraCaptureService::acceptFrame(const QImage &frame)
{
    if (!m_snapshot.connected || frame.isNull())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    ++m_fpsWindowFrames;
    const qint64 elapsed = now - m_fpsWindowStartMs;
    if (elapsed >= 500)
    {
        const double fps = 1000.0 * m_fpsWindowFrames / static_cast<double>(elapsed);
        m_snapshot.frameRate = QStringLiteral("%1 FPS").arg(fps, 0, 'f', 1);
        m_fpsWindowStartMs = now;
        m_fpsWindowFrames = 0;
    }
    m_snapshot.lastFrame = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    emit frameReady(frame);
    emit snapshotChanged(m_snapshot);
}

} // namespace rov
