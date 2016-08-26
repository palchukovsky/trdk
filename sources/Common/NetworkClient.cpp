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
#include "NetworkClient.hpp"
#include "NetworkClientService.hpp"
#include "NetworkClientServiceIo.hpp"
#include "SysError.hpp"

using namespace trdk;
using namespace trdk::Lib;

namespace io = boost::asio;
namespace ip = io::ip;
namespace pt = boost::posix_time;

////////////////////////////////////////////////////////////////////////////////

namespace {

	NetworkClient::Exception GetNetworkException(
			const boost::system::error_code &error,
			const char *what) {
		boost::format errorText("%1%: \"%2%\", (network error: \"%3%\")");
		errorText % what % SysError(error.value()) % error;
		return NetworkClient::Exception(errorText.str().c_str());
	}

}

////////////////////////////////////////////////////////////////////////////////

NetworkClient::Exception::Exception(const char *what) throw()
	: Lib::Exception(what) {
	//...//
}

class NetworkClient::Implementation : private boost::noncopyable {

public:

	typedef boost::mutex BufferMutex;
	typedef BufferMutex::scoped_lock BufferLock;

	NetworkClient &m_self;

	NetworkClientService &m_service;
	io::ip::tcp::socket m_socket;

	std::pair<Buffer, Buffer> m_buffer;
	BufferMutex m_bufferMutex;

	explicit Implementation(NetworkClientService &service, NetworkClient &self)
		: m_self(self)
		, m_service(service)
		, m_socket(m_service.GetIo().GetService()) {
		//...//
	}

	void StartRead(
			Buffer &activeBuffer,
			size_t bufferStartOffset,
			Buffer &nextBuffer) {
	
		AssertLt(bufferStartOffset, activeBuffer.size());
	
		auto self = m_self.shared_from_this();
		using Handler
			= boost::function<void(const boost::system::error_code &, size_t)>;
		const Handler &handler = [
				self,
				&activeBuffer,
				bufferStartOffset,
				&nextBuffer] (
					const boost::system::error_code &error,
					size_t transferredBytes) {
			self->m_pimpl->OnReadCompleted(
				activeBuffer,
				bufferStartOffset,
				nextBuffer,
				error,
				transferredBytes);
		};
		
		io::async_read(
			m_socket,
 			io::buffer(
				activeBuffer.data() + bufferStartOffset,
				activeBuffer.size() - bufferStartOffset),
			io::transfer_at_least(1),
			boost::bind(
				handler,
				io::placeholders::error,
				io::placeholders::bytes_transferred));

	}

	void OnReadCompleted(
			Buffer &activeBuffer,
			size_t bufferStartOffset,
			Buffer &nextBuffer,
			const boost::system::error_code &error,
			size_t transferredBytes) {

		const auto &timeMeasurement = m_self.StartMessageMeasurement();

		if (error) {
			OnConnectionError(error);
			return;
		} else if (!transferredBytes) {
			m_self.LogInfo("Connection was gratefully closed.");
			m_service.OnDisconnect();
			return;
		}

		// Checking for a message at the buffer end which wasn't fully received.
		const auto &transferedRend = activeBuffer.crend() - bufferStartOffset;
		const auto &transferedRbegin = transferedRend - transferredBytes;
		const auto &lastMessageRbegin = m_self.FindMessageEnd(
			transferedRbegin,
			transferedRend);
		const size_t unreceivedMessageLen = lastMessageRbegin - transferedRbegin;

		// To synchronizes fast data packets it should be stopped before 1st
		// StartRead and buffers size change.
		const BufferLock bufferLock(m_bufferMutex);

		if (unreceivedMessageLen > 0) {

			// At the end of the buffer located message start without end. That
			// means that the buffer is too small to receive all messages.
		
			if (unreceivedMessageLen >= transferredBytes) {
				// Buffer not too small to receive all messages - it too small to
				// receive one message.
				AssertEq(transferredBytes, unreceivedMessageLen);
				Assert(lastMessageRbegin == transferedRend);
				const auto newSize = activeBuffer.size() * 2;
				boost::format message(
					"Receiving large message (more then %1$.02f kilobytes)..."
						" To optimize reading buffer will be increased"
						": %1$.02f -> %2$.02f kilobytes.");
				message
					% (double(transferredBytes) / 1024)
					% (double(newSize) / 1024);
				m_self.LogWarn(message.str());
				activeBuffer.resize(newSize);
				nextBuffer.resize(newSize);
				// To the active buffer added more space at the end, so it can
				// continue to receive the current message in the active buffer.
				StartRead(
					activeBuffer,
					bufferStartOffset + transferredBytes,
					nextBuffer);
				// Have no fully received messages in this buffer.
				return;
			}
			
			if (bufferStartOffset + transferredBytes == activeBuffer.size()) {
				nextBuffer.clear();
				nextBuffer.resize(activeBuffer.size() * 2);
			}
			const auto &transferedEnd
				= activeBuffer.cbegin() + bufferStartOffset + transferredBytes;
			std::copy(
				transferedEnd - unreceivedMessageLen,
				transferedEnd,
				nextBuffer.begin());
			transferredBytes -= unreceivedMessageLen;

		}

		StartRead(nextBuffer, unreceivedMessageLen, activeBuffer);

		Assert(transferedRend > lastMessageRbegin);
		Assert(lastMessageRbegin.base() < activeBuffer.cend());
		Assert(
			activeBuffer.cbegin() + bufferStartOffset
			< lastMessageRbegin.base());
		m_self.HandleNewMessages(
			activeBuffer.cbegin() + bufferStartOffset,
			lastMessageRbegin.base(),
			timeMeasurement);

		if (activeBuffer.size() != nextBuffer.size()) {
			AssertLt(activeBuffer.size(), nextBuffer.size());
			{
				boost::format message(
					"Growing connection buffer"
						": %1$.02f -> %2$.02f kilobytes...");
				message
					% (double(activeBuffer.size()) / 1024)
					% (double(nextBuffer.size()) / 1024);
				m_self.LogWarn(message.str());
			}
			activeBuffer.clear();
			activeBuffer.resize(nextBuffer.size());
		}

	}

	void OnConnectionError(const boost::system::error_code &error) {
		Assert(error);
		m_socket.close();
		{
			boost::format message(
				"Connection to server closed by error:"
					" \"%1%\", (network error: \"%2%\")");
			message % SysError(error.value()) % error;
			m_self.LogError(message.str());
		}
		m_service.OnDisconnect();
	}

};

NetworkClient::NetworkClient(
		NetworkClientService &service,
		const std::string &host,
		size_t port)
	: m_pimpl(new Implementation(service, *this)) {

	const ip::tcp::resolver::query query(
		host,
		boost::lexical_cast<std::string>(port));

	boost::system::error_code error;
	io::connect(
		m_pimpl->m_socket,
		ip::tcp::resolver(m_pimpl->m_service.GetIo().GetService())
			.resolve(query),
		error);
	if (error) {
		throw GetNetworkException(error, "Failed to connect");
	}

}

NetworkClient::~NetworkClient() {
	//...//
}

NetworkClientService & NetworkClient::GetService() {
	return m_pimpl->m_service;
}
const NetworkClientService & NetworkClient::GetService() const {
	return const_cast<NetworkClient *>(this)->GetService();
}

void NetworkClient::Start() {

	AssertEq(0, m_pimpl->m_buffer.first.size());
	AssertEq(0, m_pimpl->m_buffer.second.size());

	{
		const auto timeoutMilliseconds = 15 * 1000;
#		ifdef BOOST_WINDOWS
			const DWORD timeout = timeoutMilliseconds;
#		else
			const timeval timeout = {timeoutMilliseconds / 1000};
#		endif
		auto setsockoptResult = setsockopt(
			m_pimpl->m_socket.native_handle(),
			SOL_SOCKET,
			SO_RCVTIMEO,
			reinterpret_cast<const char *>(&timeout),
			sizeof(timeout));
		if (setsockoptResult) {
			boost::format message("Failed to set SO_RCVTIMEO: \"%1%\".");
			message % setsockoptResult;
			LogError(message.str());
		}
		setsockoptResult = setsockopt(
			m_pimpl->m_socket.native_handle(),
			SOL_SOCKET,
			SO_SNDTIMEO,
			reinterpret_cast<const char *>(&timeout),
			sizeof(timeout));
		if (setsockoptResult) {
			boost::format message("Failed to set SO_SNDTIMEO: \"%1%\".");
			message % setsockoptResult;
			LogError(message.str());
		}
	}

	OnStart();

#	if DEV_VER
		const size_t initiaBufferSize = 256;
#	else
		const size_t initiaBufferSize = 1024 * 1024;
#	endif
	m_pimpl->m_buffer.first.resize(initiaBufferSize);
	m_pimpl->m_buffer.second.resize(m_pimpl->m_buffer.first.size());

	m_pimpl->StartRead(m_pimpl->m_buffer.first, 0, m_pimpl->m_buffer.second);

}

bool NetworkClient::CheckResponceSynchronously(
		const char *actionName,
		const char *expectedResponse,
		const char *errorResponse) {

	Assert(expectedResponse);
	Assert(strlen(expectedResponse));
	Assert(actionName);
	Assert(strlen(actionName));
	Assert(!errorResponse || strlen(errorResponse));

	const size_t expectedResponseSize = strlen(expectedResponse);

	std::vector<char> serverResponse(
		errorResponse
			?	std::max(strlen(errorResponse), expectedResponseSize)
			:	expectedResponseSize);
	boost::system::error_code error;
	const auto &serverResponseSize = io::read(
		m_pimpl->m_socket,
		io::buffer(serverResponse),
		io::transfer_at_least(1),
		error);
	AssertLe(serverResponseSize, serverResponse.size());

	if (error) {
		boost::format message(
			"Failed to read %1% response: \"%2%\", (network error: \"%3%\")");
		message % actionName % SysError(error.value()) % error;
		throw NetworkClient::Exception(message.str().c_str());
	}

	if (!serverResponseSize) {
		boost::format message("Connection closed by server at %1%");
		message % actionName;
		throw NetworkClient::Exception(message.str().c_str());
	}

	if (
			errorResponse
			&& strlen(errorResponse) == serverResponseSize
			&& !memcmp(&serverResponse[0], errorResponse, serverResponseSize)) {
		return false;
	}

	if (
			serverResponseSize != expectedResponseSize
			||	memcmp(
					&serverResponse[0],
					expectedResponse,
					serverResponseSize)) {
		{
			serverResponse[serverResponseSize] = 0;
			boost::format logMessage(
				"Unexpected %1% response from server (size: %2% bytes)"
					": \"%3%\".");
			logMessage % actionName % serverResponseSize % &serverResponse[0];
			LogError(logMessage.str());
		}
		boost::format logMessage("Unexpected %1% response from server");
		logMessage % actionName;
		throw NetworkClient::Exception(logMessage.str().c_str());
	}

	return true;

}

void NetworkClient::SendSynchronously(
		const std::string &message,
		const char *requestName) {

	Assert(requestName);
	Assert(strlen(requestName));

	boost::system::error_code error;
	const auto size = io::write(
		m_pimpl->m_socket,
		io::buffer(message),
		error);

	if (error || size != message.size()) {
		{
			boost::format logMessage(
				"Failed to send %1%: \"%2%\", (network error: \"%3%\")."
					" Message size: %4% bytes, sent: %5% bytes.");
			logMessage
				% requestName
				% SysError(error.value())
				% error
				% message.size()
				% size;
			LogError(logMessage.str());
		}
		boost::format exceptionMessage("Failed to send %1%");
		exceptionMessage % requestName;
		throw NetworkClient::Exception(exceptionMessage.str().c_str());
	}

}

bool NetworkClient::RequestSynchronously(
		const std::string &message,
		const char *requestName,
		const char *expectedResponse,
		const char *errorResponse) {
	SendSynchronously(message, requestName);
	return CheckResponceSynchronously(
		requestName,
		expectedResponse,
		errorResponse);
}
