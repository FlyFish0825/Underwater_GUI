#include "communication/transport/SerialTransport.h"

#include <QRegularExpression>
#include <QTimer>

#ifdef Q_OS_WIN
#    include <windows.h>
#    include <setupapi.h>
#    include <devguid.h>
#endif

namespace rov
{

QString SerialDeviceInfo::vidPidText() const
{
    return QStringLiteral("VID_%1 PID_%2")
        .arg(vendorId, 4, 16, QLatin1Char('0'))
        .arg(productId, 4, 16, QLatin1Char('0'))
        .toUpper();
}

class SerialTransport::Impl
{
  public:
#ifdef Q_OS_WIN
    HANDLE handle = INVALID_HANDLE_VALUE;
#endif
    QString portName;
    QTimer *timer = nullptr;
};

#ifdef Q_OS_WIN
namespace
{

QString wideStringProperty(HDEVINFO info, SP_DEVINFO_DATA &data, DWORD property)
{
    DWORD type = 0;
    DWORD bytes = 0;
    SetupDiGetDeviceRegistryPropertyW(info, &data, property, &type, nullptr, 0, &bytes);
    if (bytes == 0)
        return {};
    QVector<wchar_t> buffer(static_cast<int>(bytes / sizeof(wchar_t) + 1));
    if (!SetupDiGetDeviceRegistryPropertyW(info, &data, property, &type,
                                           reinterpret_cast<PBYTE>(buffer.data()), bytes, &bytes))
        return {};
    return QString::fromWCharArray(buffer.constData());
}

QString devicePortName(HDEVINFO info, SP_DEVINFO_DATA &data)
{
    HKEY key = SetupDiOpenDevRegKey(info, &data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return {};
    wchar_t value[256] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, L"PortName", nullptr, &type,
                                            reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? QString::fromWCharArray(value) : QString();
}

void parseVidPid(const QString &hardwareIds, quint16 &vid, quint16 &pid)
{
    const auto match = QRegularExpression(QStringLiteral("VID_([0-9A-Fa-f]{4}).*PID_([0-9A-Fa-f]{4})"))
                           .match(hardwareIds);
    if (match.hasMatch())
    {
        vid = static_cast<quint16>(match.captured(1).toUShort(nullptr, 16));
        pid = static_cast<quint16>(match.captured(2).toUShort(nullptr, 16));
    }
}

} // namespace
#endif

SerialTransport::SerialTransport(QObject *parent) : QObject(parent), m_impl(new Impl)
{
    m_impl->timer = new QTimer(this);
    m_impl->timer->setInterval(20);
    connect(m_impl->timer, &QTimer::timeout, this, &SerialTransport::pollRead);
}

SerialTransport::~SerialTransport()
{
    close();
    delete m_impl;
}

QVector<SerialDeviceInfo> SerialTransport::enumerate(quint16 vendorId, quint16 productId)
{
    QVector<SerialDeviceInfo> result;
#ifdef Q_OS_WIN
    HDEVINFO info = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE)
        return result;
    for (DWORD index = 0;; ++index)
    {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(info, index, &data))
            break;
        const QString hardwareIds = wideStringProperty(info, data, SPDRP_HARDWAREID);
        SerialDeviceInfo device;
        parseVidPid(hardwareIds, device.vendorId, device.productId);
        if (device.vendorId != vendorId || device.productId != productId)
            continue;
        device.portName = devicePortName(info, data);
        device.displayName = wideStringProperty(info, data, SPDRP_FRIENDLYNAME);
        device.manufacturer = wideStringProperty(info, data, SPDRP_MFG);
        if (!device.portName.isEmpty())
            result.append(device);
    }
    SetupDiDestroyDeviceInfoList(info);
#else
    Q_UNUSED(vendorId)
    Q_UNUSED(productId)
#endif
    return result;
}

bool SerialTransport::open(const SerialDeviceInfo &device)
{
    close();
#ifdef Q_OS_WIN
    QString path = device.portName;
    if (!path.startsWith(QStringLiteral("\\\\.\\")))
        path = QStringLiteral("\\\\.\\") + path;
    m_impl->handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                                 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_impl->handle == INVALID_HANDLE_VALUE)
    {
        emit errorOccurred(QStringLiteral("无法打开 %1（错误码 %2）")
                               .arg(device.portName)
                               .arg(GetLastError()));
        return false;
    }
    SetupComm(m_impl->handle, 4096, 4096);
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(m_impl->handle, &dcb))
    {
        emit errorOccurred(QStringLiteral("读取 %1 串口参数失败").arg(device.portName));
        close();
        return false;
    }
    dcb.BaudRate = CBR_115200; // USB CDC 不使用波特率，此值仅满足 Win32 串口接口要求。
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState(m_impl->handle, &dcb))
    {
        emit errorOccurred(QStringLiteral("配置 %1 串口失败").arg(device.portName));
        close();
        return false;
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    SetCommTimeouts(m_impl->handle, &timeouts);
    PurgeComm(m_impl->handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    m_impl->portName = device.portName;
    m_impl->timer->start();
    emit opened(m_impl->portName);
    return true;
#else
    Q_UNUSED(device)
    emit errorOccurred(QStringLiteral("当前平台未实现 Windows 虚拟串口传输"));
    return false;
#endif
}

void SerialTransport::close()
{
    if (!isOpen())
        return;
    m_impl->timer->stop();
#ifdef Q_OS_WIN
    CloseHandle(m_impl->handle);
    m_impl->handle = INVALID_HANDLE_VALUE;
#endif
    m_impl->portName.clear();
    emit closed();
}

bool SerialTransport::isOpen() const
{
#ifdef Q_OS_WIN
    return m_impl->handle != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

QString SerialTransport::portName() const
{
    return m_impl->portName;
}

bool SerialTransport::writeBytes(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    if (!isOpen())
    {
        emit errorOccurred(QStringLiteral("串口尚未连接"));
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(m_impl->handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        || written != static_cast<DWORD>(bytes.size()))
    {
        emit errorOccurred(QStringLiteral("串口发送失败"));
        return false;
    }
    return true;
#else
    Q_UNUSED(bytes)
    return false;
#endif
}

void SerialTransport::pollRead()
{
#ifdef Q_OS_WIN
    if (!isOpen())
        return;
    DWORD available = 0;
    COMSTAT status{};
    DWORD errors = 0;
    if (!ClearCommError(m_impl->handle, &errors, &status))
    {
        emit errorOccurred(QStringLiteral("读取串口状态失败"));
        close();
        return;
    }
    available = status.cbInQue;
    while (available > 0)
    {
        QByteArray buffer(static_cast<int>(qMin<DWORD>(available, 4096)), Qt::Uninitialized);
        DWORD read = 0;
        if (!ReadFile(m_impl->handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            emit errorOccurred(QStringLiteral("读取串口数据失败"));
            close();
            return;
        }
        if (read > 0)
            emit bytesReceived(buffer.left(static_cast<int>(read)));
        if (read == 0)
            break;
        available -= qMin<DWORD>(available, read);
    }
#endif
}

} // namespace rov
