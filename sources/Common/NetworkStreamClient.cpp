/**************************************************************************
 *   Created: 2016/08/24 13:30:26
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "NetworkStreamClient.hpp"
#include "NetworkClientServiceIo.hpp"
#include "NetworkStreamClientService.hpp"
#include "SysError.hpp"

using namespace trdk::Lib;

namespace io = boost::asio;
namespace ip = io::ip;
namespace pt = boost::posix_time;

////////////////////////////////////////////////////////////////////////////////

NetworkStreamClient::Exception::Exception(const char *what) noexcept
    : Lib::Exception(what) {}

NetworkStreamClient::ConnectError::ConnectError(const char *what) noexcept
    : Exception(what) {}

NetworkStreamClient::ProtocolError::ProtocolError(const char *what,
                                                  const char *bufferAddress,
                                                  char expectedByte) noexcept
    : Exception(what),
      m_bufferAddress(bufferAddress),
      m_expectedByte(expectedByte) {}
const char *NetworkStreamClient::ProtocolError::GetBufferAddress() const {
  return m_bufferAddress;
}
char NetworkStreamClient::ProtocolError::GetExpectedByte() const {
  return m_expectedByte;
}

class NetworkStreamClient::Implementation : private boost::noncopyable {
 public:
  NetworkStreamClient &m_self;

  NetworkStreamClientService &m_service;
  const std::unique_ptr<NetworkClientServiceIo> m_io;

  std::pair<Buffer, Buffer> m_buffer;
  BufferMutex m_bufferMutex;

  size_t m_numberOfReceivedBytes;

  explicit Implementation(NetworkStreamClientService &service,
                          NetworkStreamClient &self)
      : m_self(self),
        m_service(service),
        m_io(m_service.CreateIo()),
        m_numberOfReceivedBytes(0) {}

  template <typename Message>
  void SendSynchronously(const Message &message, const char *requestName) {
    AssertLt(0, message.size());
    Assert(requestName);
    Assert(strlen(requestName));
    // Available only before asynchronous mode start:
    AssertEq(0, m_buffer.first.size());
    AssertEq(0, m_buffer.second.size());

    const auto &result = m_io->Write(io::buffer(message));
    const auto &error = result.first;
    const auto &size = result.second;
    if (error || size != message.size()) {
      {
        boost::format logMessage(
            "%1%Failed to send %2%: \"%3%\", (network error: \"%4%\")."
            " Message size: %5% bytes, sent: %6% bytes.");
        logMessage % m_self.GetLogTag() % requestName %
            SysError(error.value()) % error % message.size() % size;
        m_self.LogError(logMessage.str());
      }
      boost::format exceptionMessage("Failed to send %1%");
      exceptionMessage % requestName;
      throw Exception(exceptionMessage.str().c_str());
    }
  }

  void Send(const std::vector<io::const_buffer> &&bufferSequnce,
            const boost::function<void()> &&onOperaton—ompletion) {
    Assert(!bufferSequnce.empty());
    // Available only after asynchronous mode start:
    AssertLt(0, m_buffer.first.size());
    AssertLt(0, m_buffer.second.size());

    const auto &self = m_self.shared_from_this();

    try {
      m_io->StartAsyncWrite(std::move(bufferSequnce), [
        self, onOperaton—ompletion
      ](const boost::system::error_code &error) {
        onOperaton—ompletion();
        if (error) {
          self->m_pimpl->OnConnectionError(error);
        }
      });
    } catch (const std::exception &ex) {
      boost::format error("Failed to write to socket: \"%1%\"");
      error % ex.what();
      throw Exception(error.str().c_str());
    }
  }

  template <typename Message>
  void Send(Message &&message) {
    Assert(!message.empty());
    // Available only after asynchronous mode start:
    AssertLt(0, m_buffer.first.size());
    AssertLt(0, m_buffer.second.size());

    const auto &self = m_self.shared_from_this();
    const auto &messageCopy = boost::make_shared<Message>(std::move(message));

    try {
      m_io->StartAsyncWrite(
          io::buffer(*messageCopy),
          [self, messageCopy](const boost::system::error_code &error) {
            if (error) {
              self->m_pimpl->OnConnectionError(error);
            }
          });
    } catch (const std::exception &ex) {
      boost::format error("Failed to write to socket: \"%1%\"");
      error % ex.what();
      throw Exception(error.str().c_str());
    }
  }

  void Send(const char *persistentBuffer, size_t len) {
    Assert(persistentBuffer);
    AssertLt(0, len);
    // Available only after asynchronous mode start:
    AssertLt(0, m_buffer.first.size());
    AssertLt(0, m_buffer.second.size());

    const auto &self = m_self.shared_from_this();

    try {
      m_io->StartAsyncWrite(io::buffer(persistentBuffer, len),
                            [self](const boost::system::error_code &error) {
                              if (error) {
                                self->m_pimpl->OnConnectionError(error);
                              }
                            });
    } catch (const std::exception &ex) {
      boost::format error("Failed to write to socket: \"%1%\"");
      error % ex.what();
      throw Exception(error.str().c_str());
    }
  }

  void StartRead(Buffer &activeBuffer,
                 size_t bufferStartOffset,
                 Buffer &nextBuffer) {
    AssertLt(bufferStartOffset, activeBuffer.size());

#ifdef DEV_VER
    std::fill(activeBuffer.begin() + bufferStartOffset, activeBuffer.end(),
              static_cast<char>(-1));
#endif

    auto self = m_self.shared_from_this();

    try {
      m_io->StartAsyncRead(
          io::buffer(activeBuffer.data() + bufferStartOffset,
                     activeBuffer.size() - bufferStartOffset),
          io::transfer_at_least(1),
          [self, &activeBuffer, bufferStartOffset, &nextBuffer](
              const boost::system::error_code &error, size_t transferredBytes) {
            self->m_pimpl->OnReadCompleted(activeBuffer, bufferStartOffset,
                                           nextBuffer, error, transferredBytes);
          });
    } catch (const std::exception &ex) {
      boost::format message("Failed to start read: \"%1%\"");
      message % ex.what();
      throw Exception(message.str().c_str());
    }
  }

  void OnReadCompleted(Buffer &activeBuffer,
                       size_t bufferStartOffset,
                       Buffer &nextBuffer,
                       const boost::system::error_code &error,
                       size_t transferredBytes) {
    const auto &timeMeasurement = m_self.StartMessageMeasurement();
    const auto &now = m_self.GetCurrentTime();

    if (error) {
      OnConnectionError(error);
      return;
    } else if (!transferredBytes) {
      {
        boost::format message(
            "%1%Connection was gratefully closed."
            " Received %2$.02f %3%.");
        message % m_self.GetLogTag();
        const auto &stat = m_self.GetReceivedVerbouseStat();
        message % stat.first % stat.second;
        m_self.LogInfo(message.str().c_str());
      }
      m_service.OnDisconnect();
      return;
    }

    // Checking for a message at the buffer end which wasn't fully received.
    const auto &transferedBegin = activeBuffer.cbegin() + bufferStartOffset;
    const auto &transferedEnd = transferedBegin + transferredBytes;

    // To synchronizes fast data packets it should be stopped before 1st
    // StartRead and buffers size change. Also, if FindLastMessageLastByte
    // reads state that can be set in HandleNewMessages - this two
    // operations should synced too.
    const BufferLock bufferLock(m_bufferMutex);

    m_numberOfReceivedBytes += transferredBytes;

    Buffer::const_iterator lastMessageLastByte;
    try {
      lastMessageLastByte = m_self.FindLastMessageLastByte(
          activeBuffer.cbegin(), transferedBegin, transferedEnd);
    } catch (const trdk::Lib::NetworkStreamClient::ProtocolError &ex) {
      Dump(ex, activeBuffer.cbegin(), transferedEnd);
      throw Exception("Protocol error");
    }
    Assert(transferedBegin <= lastMessageLastByte);
    Assert(lastMessageLastByte <= transferedEnd);

    const auto bufferedSize = bufferStartOffset + transferredBytes;
    const auto unreceivedMessageLen =
        lastMessageLastByte == transferedEnd
            ? bufferedSize
            : transferedEnd - std::next(lastMessageLastByte);
    AssertLe(unreceivedMessageLen, activeBuffer.size());

    if (unreceivedMessageLen > 0) {
      const auto freeBufferSpace = activeBuffer.size() - bufferedSize;

      // At the end of the buffer located message start without end. That
      // means that the buffer is too small to receive all messages.

      if (unreceivedMessageLen >= bufferedSize) {
        AssertEq(unreceivedMessageLen, bufferedSize);
        // Buffer not too small to receive all messages - it too small
        // to receive one message.
        if (unreceivedMessageLen / 3 > freeBufferSpace) {
          const auto newSize = activeBuffer.size() * 2;
#ifndef _DEBUG
          {
            boost::format message(
                "%1%Receiving large message in %2$.02f kilobytes..."
                " To optimize reading buffer 0x%3% will"
                " be increased: %4$.02f -> %5$.02f kilobytes."
                " Total received volume: %6$.02f %7%.");
            message % m_self.GetLogTag() %
                (double(unreceivedMessageLen) / 1024) % &activeBuffer %
                (double(activeBuffer.size()) / 1024) % (double(newSize) / 1024);
            const auto &stat = m_self.GetReceivedVerbouseStat();
            message % stat.first % stat.second;
            m_self.LogWarn(message.str());
          }
#endif
          if (newSize > (1024 * 1024) * 20) {
            throw Exception("The maximum buffer size is exceeded.");
          }
          activeBuffer.resize(newSize);
          nextBuffer.resize(newSize);
          // To the active buffer added more space at the end, so it
          // can continue to receive the current message in the active
          // buffer.
        }
        StartRead(activeBuffer, bufferStartOffset + transferredBytes,
                  nextBuffer);
        // Have no fully received messages in this buffer.
        return;
      }

      if (freeBufferSpace == 0) {
        nextBuffer.clear();
        const auto newSize = activeBuffer.size() * 2;
#ifndef _DEBUG
        {
          boost::format message(
              "%1%Increasing buffer 0x%2% size:"
              " %3$.02f -> %4$.02f kilobytes."
              " Total received volume: %5$.02f %6%.");
          message % m_self.GetLogTag() % &nextBuffer %
              (double(activeBuffer.size()) / 1024) % (double(newSize) / 1024);
          const auto &stat = m_self.GetReceivedVerbouseStat();
          message % stat.first % stat.second;
          m_self.LogDebug(message.str());
        }
#endif
        nextBuffer.resize(newSize);
      }

#ifndef _DEBUG
      if (unreceivedMessageLen >= 10 * 1024) {
        boost::format message(
            "%1%Restoring buffer content in %2$.02f kilobytes"
            " to continue to receive message..."
            " Total received volume: %3$.02f %4%.");
        message % m_self.GetLogTag() % (double(unreceivedMessageLen) / 1024);
        const auto &stat = m_self.GetReceivedVerbouseStat();
        message % stat.first % stat.second;
        m_self.LogDebug(message.str());
      }
#endif
      AssertGe(nextBuffer.size(), unreceivedMessageLen);
      std::copy(transferedEnd - unreceivedMessageLen, transferedEnd,
                nextBuffer.begin());
    }

    StartRead(nextBuffer, unreceivedMessageLen, activeBuffer);

    try {
      m_self.HandleNewMessages(now, activeBuffer.cbegin(), lastMessageLastByte,
                               timeMeasurement);
    } catch (const trdk::Lib::NetworkStreamClient::ProtocolError &ex) {
      Dump(ex, activeBuffer.cbegin(), lastMessageLastByte);
      throw Exception("Protocol error");
    }

    // activeBuffer.size() may be greater if handler of prev thread raised
    // exception after nextBuffer has been resized.
    if (activeBuffer.size() <= nextBuffer.size()) {
      activeBuffer.clear();
      activeBuffer.resize(nextBuffer.size());
    }
  }

  void OnConnectionError(const boost::system::error_code &error) {
    Assert(error);
    m_io->Close();
    {
      boost::format message(
          "%1%Connection to server closed by error:"
          " \"%2%\", (network error: \"%3%\")."
          " Received %4$.02f %5%.");
      message % m_self.GetLogTag() % SysError(error.value()) % error;
      const auto &stat = m_self.GetReceivedVerbouseStat();
      message % stat.first % stat.second;
      m_self.LogError(message.str());
    }
    m_service.OnDisconnect();
  }

  void Dump(const NetworkStreamClient::ProtocolError &ex,
            const Buffer::const_iterator &begin,
            const Buffer::const_iterator &end) const {
    std::ostringstream ss;
    ss << m_self.GetLogTag() << "Protocol error: \"" << ex << "\".";

    ss << " Active buffer: [ ";
    Assert(&*begin < ex.GetBufferAddress());
    Assert(ex.GetBufferAddress() <= &*(end));
    Assert(ex.GetBufferAddress() < &*(end));
    for (auto it = begin; it < end; ++it) {
      const bool isHighlighted = &*it == ex.GetBufferAddress();
      if (isHighlighted) {
        ss << '<';
      }
      ss << std::hex << std::setfill('0') << std::setw(2) << (*it & 0xff);
      if (isHighlighted) {
        ss << '>';
      }
      ss << ' ';
    }
    ss << "].";

    ss << " Expected byte: 0x" << std::hex << std::setfill('0') << std::setw(2)
       << (ex.GetExpectedByte() & 0xff) << '.';

    m_self.LogError(ss.str());
  }
};

NetworkStreamClient::NetworkStreamClient(NetworkStreamClientService &service,
                                         const std::string &host,
                                         size_t port)
    : m_pimpl(new Implementation(service, *this)) {
  try {
    const ip::tcp::resolver::query query(
        host, boost::lexical_cast<std::string>(port));

    {
      const auto &error = m_pimpl->m_io->Connect(std::move(query));
      if (error) {
        boost::format errorText("\"%1%\" (network error: \"%2%\")");
        errorText % SysError(error.value()) % error;
        throw ConnectError(errorText.str().c_str());
      }
    }

  } catch (const std::exception &ex) {
    throw ConnectError(ex.what());
  }
}

NetworkStreamClient::~NetworkStreamClient() {
  try {
    m_pimpl->m_service.OnClientDestroy();
  } catch (...) {
    AssertFailNoException();
    terminate();
  }
}

const std::string &NetworkStreamClient::GetLogTag() const {
  return GetService().GetLogTag();
}

size_t NetworkStreamClient::GetNumberOfReceivedBytes() const {
  return m_pimpl->m_numberOfReceivedBytes;
}

NetworkStreamClientService &NetworkStreamClient::GetService() {
  return m_pimpl->m_service;
}
const NetworkStreamClientService &NetworkStreamClient::GetService() const {
  return const_cast<NetworkStreamClient *>(this)->GetService();
}

void NetworkStreamClient::Start() {
  AssertEq(0, m_pimpl->m_buffer.first.size());
  AssertEq(0, m_pimpl->m_buffer.second.size());

  {
    const auto timeoutMilliseconds = 15 * 1000;
#ifdef BOOST_WINDOWS
    const DWORD timeout = timeoutMilliseconds;
#else
    const timeval timeout = {timeoutMilliseconds / 1000};
#endif
    auto setsockoptResult =
        setsockopt(m_pimpl->m_io->GetNativeHandle(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    if (setsockoptResult) {
      boost::format message("%1%Failed to set SO_RCVTIMEO: \"%2%\".");
      message % GetLogTag() % setsockoptResult;
      LogError(message.str());
    }
    setsockoptResult =
        setsockopt(m_pimpl->m_io->GetNativeHandle(), SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    if (setsockoptResult) {
      boost::format message("%1%Failed to set SO_SNDTIMEO: \"%2%\".");
      message % GetLogTag() % setsockoptResult;
      LogError(message.str());
    }
  }

  OnStart();

#if DEV_VER
  const size_t initiaBufferSize = 256;
#else
  const size_t initiaBufferSize = (1024 * 1024) * 2;
#endif
  m_pimpl->m_buffer.first.resize(initiaBufferSize);
  m_pimpl->m_buffer.second.resize(m_pimpl->m_buffer.first.size());
#ifdef DEV_VER
  std::fill(m_pimpl->m_buffer.first.begin(), m_pimpl->m_buffer.first.end(),
            static_cast<char>(-1));
  std::fill(m_pimpl->m_buffer.second.begin(), m_pimpl->m_buffer.second.end(),
            static_cast<char>(-1));
#endif

  m_pimpl->StartRead(m_pimpl->m_buffer.first, 0, m_pimpl->m_buffer.second);
}

void NetworkStreamClient::Stop() {
  if (!m_pimpl->m_io->IsOpen()) {
    return;
  }
  {
    boost::format message("%1%Closing connection...");
    message % GetLogTag();
    LogInfo(message.str().c_str());
  }
  m_pimpl->m_io->Shutdown(io::ip::tcp::socket::shutdown_both);
  m_pimpl->m_io->Close();
}

bool NetworkStreamClient::CheckResponseSynchronously(
    const char *actionName,
    const char *expectedResponse,
    const char *errorResponse) {
  Assert(expectedResponse);
  Assert(strlen(expectedResponse));
  Assert(actionName);
  Assert(strlen(actionName));
  Assert(!errorResponse || strlen(errorResponse));

  // Available only before asynchronous mode start:
  AssertEq(0, m_pimpl->m_buffer.first.size());
  AssertEq(0, m_pimpl->m_buffer.second.size());

  const size_t expectedResponseSize = strlen(expectedResponse);

  const auto &serverResponse = ReceiveSynchronously(
      actionName,
      errorResponse ? std::max(strlen(errorResponse), expectedResponseSize)
                    : expectedResponseSize);

  if (errorResponse && strlen(errorResponse) == serverResponse.size() &&
      !memcmp(&serverResponse[0], errorResponse, serverResponse.size())) {
    return false;
  }

  if (serverResponse.size() != expectedResponseSize ||
      memcmp(&serverResponse[0], expectedResponse, serverResponse.size())) {
    {
      boost::format logMessage(
          "%1%Unexpected %2% response from server (size: %3% bytes)"
          ": \"%4%\".");
      logMessage % GetLogTag()                                            // 1
          % actionName                                                    // 2
          % serverResponse.size()                                         // 3
          % std::string(serverResponse.cbegin(), serverResponse.cend());  // 4
      LogError(logMessage.str());
    }
    boost::format logMessage("Unexpected %1% response from server");
    logMessage % actionName;
    throw Exception(logMessage.str().c_str());
  }

  return true;
}

void NetworkStreamClient::Send(
    const std::vector<io::const_buffer> &&bufferSequnce,
    const boost::function<void()> &&onOperaton—ompletion) {
  m_pimpl->Send(std::move(bufferSequnce), std::move(onOperaton—ompletion));
}
void NetworkStreamClient::Send(const std::vector<char> &&message) {
  m_pimpl->Send(std::move(message));
}
void NetworkStreamClient::Send(const std::string &&message) {
  m_pimpl->Send(std::move(message));
}
void NetworkStreamClient::Send(const char *persistentBuffer, size_t len) {
  m_pimpl->Send(persistentBuffer, len);
}

void NetworkStreamClient::SendSynchronously(const std::vector<char> &message,
                                            const char *requestName) {
  m_pimpl->SendSynchronously(message, requestName);
}
void NetworkStreamClient::SendSynchronously(const std::string &message,
                                            const char *requestName) {
  m_pimpl->SendSynchronously(message, requestName);
}

std::vector<char> NetworkStreamClient::ReceiveSynchronously(
    const char *requestName, size_t size) {
  Assert(requestName);
  Assert(strlen(requestName));

  // Available only before asynchronous mode start:
  AssertEq(0, m_pimpl->m_buffer.first.size());
  AssertEq(0, m_pimpl->m_buffer.second.size());

  std::vector<char> result(size);

  const auto &ioResult =
      m_pimpl->m_io->Read(io::buffer(result), io::transfer_at_least(1));
  const auto &error = ioResult.first;
  const auto &serverResponseSize = ioResult.second;
  AssertLe(serverResponseSize, result.size());
  result.resize(serverResponseSize);

  if (error) {
    boost::format message(
        "Failed to read %1% response: \"%2%\", (network error: \"%3%\")");
    message % requestName % SysError(error.value()) % error;
    throw Exception(message.str().c_str());
  }

  if (!serverResponseSize) {
    boost::format message("Connection closed by server at %1%");
    message % requestName;
    throw Exception(message.str().c_str());
  }

  return result;
}

bool NetworkStreamClient::RequestSynchronously(const std::string &message,
                                               const char *requestName,
                                               const char *expectedResponse,
                                               const char *errorResponse) {
  SendSynchronously(message, requestName);
  return CheckResponseSynchronously(requestName, expectedResponse,
                                    errorResponse);
}

std::pair<double, std::string> NetworkStreamClient::GetReceivedVerbouseStat()
    const {
  if (m_pimpl->m_numberOfReceivedBytes > ((1024 * 1024) * 1024)) {
    return std::make_pair(
        double(m_pimpl->m_numberOfReceivedBytes) / ((1024 * 1024) * 1024),
        "gigabytes");
  } else if (m_pimpl->m_numberOfReceivedBytes > (1024 * 1024)) {
    return std::make_pair(
        double(m_pimpl->m_numberOfReceivedBytes) / (1024 * 1024), "megabytes");
  } else {
    return std::make_pair(double(m_pimpl->m_numberOfReceivedBytes) / 1024,
                          "kilobytes");
  }
}

NetworkStreamClient::BufferLock NetworkStreamClient::LockDataExchange() {
  return BufferLock(m_pimpl->m_bufferMutex);
}
