/**************************************************************************
 *   Created: 2015/07/20 08:19:53
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "DropCopyClient.hpp"
#include "DropCopyService.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::EngineService;
using namespace trdk::EngineService::DropCopy;
using namespace trdk::EngineService::MarketData;
using namespace trdk::EngineServer;

namespace io = boost::asio;

DropCopyClient::DropCopyClient(
		DropCopyService &service,
		const std::string &host,
		const std::string &port)
	: m_service(service),
	m_host(host),
	m_port(port),
	m_socket(m_service.GetIoService()) {
}

DropCopyClient::~DropCopyClient() {
	try {
		m_service.OnClientClose(*this);
	} catch (...) {
		AssertFailNoException();
		throw;
	}
	
}

boost::shared_ptr<DropCopyClient> DropCopyClient::Create(
		DropCopyService &service,
		const std::string &host,
		const std::string &port) {
	boost::shared_ptr<DropCopyClient> result(
		new DropCopyClient(service, host, port));
	result->Connect();
	return result;
}

void DropCopyClient::Connect() {

	m_service.GetLog().Debug("Connecting to \"%1%:%2%\"...", m_host, m_port);

	const io::ip::tcp::resolver::query query(m_host, m_port);
	boost::system::error_code error;
	io::connect(
		m_socket,
		io::ip::tcp::resolver(m_service.GetIoService()).resolve(query),
		error);
	if (error) {
		boost::format errorText("\"%1%\", (network error: \"%2%\")");
		errorText % SysError(error.value()) % error;
		throw ConnectError(errorText.str().c_str());
	}

	m_service.GetLog().Info("Connected to \"%1%:%2%\".", m_host, m_port);

	StartRead();

}

void DropCopyClient::Close() {
	m_socket.close();
}

void DropCopyClient::StartRead() {
	io::async_read(
		m_socket,
		boost::asio::buffer(
			&m_nextMessageSize,
			sizeof(m_nextMessageSize)),
		io::transfer_exactly(sizeof(m_nextMessageSize)),
		boost::bind(
			&DropCopyClient::OnNewData,
			shared_from_this(),
			io::placeholders::error));
}

void DropCopyClient::OnNewData(const boost::system::error_code &error) {

	if (error) {
		m_service.GetLog().Error(
			"Connection closed with error: \"%1%\".",
			SysError(error.value()));
		Close();
		return;
	}

	m_service.GetLog().Warn("Unexpected data from Drop Copy push-connection.");
	
	StartRead();

}

void DropCopyClient::Send(const ServiceData &message) {
	std::ostringstream oss;
	message.SerializeToOstream(&oss);
	FlushOutStream(oss.str());
}

void DropCopyClient::FlushOutStream(const std::string &s) {
	boost::shared_ptr<std::vector<char>> buffer(
		new std::vector<char>(sizeof(int32_t) + s.size()));
	*reinterpret_cast<int32_t *>(&(*buffer)[0]) = int32_t(s.size());
	memcpy(&(*buffer)[sizeof(int32_t)], s.c_str(), s.size());
	io::async_write(
		m_socket,
		io::buffer(&(*buffer)[0], buffer->size()),
		boost::bind(
			&DropCopyClient::OnDataSent,
			shared_from_this(),
			buffer,
			io::placeholders::error,
			io::placeholders::bytes_transferred));
}

void DropCopyClient::OnDataSent(
		//! @todo reimplement buffer
		const boost::shared_ptr<std::vector<char>> &,
		const boost::system::error_code &error,
		size_t /*bytesTransferred*/) {
	if (error) {
		m_service.GetLog().Error(
			"Failed to send data: \"%1%\".",
			SysError(error.value()));
	}
}