/**************************************************************************
 *   Created: 2012/09/19 23:46:49
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Service.hpp"
#include "Core/Security.hpp"

using namespace Trader::Gateway;

Service::Service(
			const std::string &tag,
			const Observer::NotifyList &notifyList,
			const IniFile &ini,
			const std::string &section)
		: Observer(tag, notifyList),
		m_stopFlag(false) {

	m_port = ini.ReadTypedKey<unsigned short>(section, "port");
			
	try {
		soap_init2(&m_soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE);
		m_soap.user = this;
		m_soap.max_keep_alive = 1000;
		StartSoapDispatcherThread();
	} catch (...) {
		soap_done(&m_soap);
		throw;
	}

}

Service::~Service() {
	Interlocking::Exchange(m_stopFlag, true);
	soap_done(&m_soap);
	{
		boost::mutex::scoped_lock lock(m_connectionRemoveMutex);
		const Connections::const_iterator end(m_connections.end());
		for (Connections::const_iterator i(m_connections.begin()); i != end; ++i) {
			soap_done(*i);
		}
	}
	m_threads.join_all();
}

const std::string & Service::GetName() const {
	static const std::string name = "Gateway";
	return name;
}

void Service::LogSoapError() const {
	std::ostringstream oss;
	soap_stream_fault(&m_soap, oss);
	std::string soapError(
		boost::regex_replace(
			oss.str(),
			boost::regex("[\n\t\r]"),
			" ",
			boost::match_default | boost::format_all));
	boost::trim(soapError);
	if (soapError.empty()) {
		soapError = "Unknown SOAP error.";
	}
	Log::Error(TRADER_GATEWAY_LOG_PREFFIX "%1%", soapError);
}

void Service::SoapServeThread(::soap *soap) {

	class Cleaner : private boost::noncopyable {
	public:
		Cleaner(::soap &soap, Connections &connections, ConnectionRemoveMutex &connectionsMutex)
				: m_soap(soap),
				m_connections(connections),
				m_connectionsMutex(connectionsMutex) {
			//...//
		}
		~Cleaner() {
			const auto ip = m_soap.ip;
			soap_destroy(&m_soap);
 			soap_end(&m_soap);
			{
				const ConnectionRemoveLock lock(m_connectionsMutex);
				const Connections::iterator pos(m_connections.find(&m_soap));
				if (pos != m_connections.end()) {
 					soap_done(&m_soap);
 					m_connections.erase(pos);
				}
			}
			free(&m_soap);
			Log::Info(
				TRADER_GATEWAY_LOG_PREFFIX "closed connection from %d.%d.%d.%d.",
				(ip >> 24) & 0xFF,
				(ip >> 16) & 0xFF,
				(ip >> 8) & 0xFF,
				ip & 0xFF);
		}
	private:
		::soap &m_soap;
		Connections &m_connections;
		ConnectionRemoveMutex &m_connectionsMutex;
	} cleaner(*soap, m_connections, m_connectionRemoveMutex);

	const int serveResult = soap_serve(soap);
	if (	serveResult != SOAP_OK
			&& serveResult != SOAP_EOF
			/*&& serveResult != SOAP_NO_METHOD*/) {
		LogSoapError();
	}

}

void Service::HandleSoapRequest() {

	SOAP_SOCKET acceptedSocket = soap_accept(&m_soap);
	if (m_stopFlag) {
		return;
	}

	if (acceptedSocket < 0) {
		LogSoapError();
		return;
	}

	Log::Info(
		TRADER_GATEWAY_LOG_PREFFIX "accepted connection from %d.%d.%d.%d.",
		(m_soap.ip >> 24) & 0xFF,
		(m_soap.ip >> 16) & 0xFF,
		(m_soap.ip >> 8) & 0xFF,
		m_soap.ip & 0xFF);

	soap *connection = soap_copy(&m_soap);
	m_connections.insert(connection);
	m_threads.create_thread(boost::bind(&Service::SoapServeThread, this, connection));

 	soap_destroy(&m_soap);
 	soap_end(&m_soap);

}

void Service::StartSoapDispatcherThread() {

	const char *const serviceHost = NULL;
	SOAP_SOCKET masterSocket = soap_bind(&m_soap, serviceHost, m_port, 100);
	if (!soap_valid_socket(masterSocket)) {
		LogSoapError();
		throw Exception("Failed to start Gateway SOAP Server");
	}

	Log::Info(TRADER_GATEWAY_LOG_PREFFIX "started at port %1%.", m_port);

	{
		sockaddr_in hostInfo;
		socklen_t hostInfoSize = sizeof(hostInfo);
		if (getsockname(masterSocket, reinterpret_cast<sockaddr *>(&hostInfo), &hostInfoSize)) {
			const Trader::Lib::Error error(GetLastError());
			Log::Error(
				TRADER_GATEWAY_LOG_PREFFIX "failed to get to get local network port info: %1%.",
				error);
			throw Exception("Failed to start Gateway SOAP Server");
		}
		m_soap.port = ntohs(hostInfo.sin_port);
	}

	m_threads.create_thread(boost::bind(&Service::SoapDispatcherThread, this));

}

void Service::SoapDispatcherThread() {
	while (!m_stopFlag) {
		HandleSoapRequest();
	}
}

namespace {

	time_t ConvertPosixTimeToTimeT(
				const boost::posix_time::ptime &posixTime) {
		namespace pt = boost::posix_time;
		Assert(!posixTime.is_special());
		static const pt::ptime timeTEpoch(boost::gregorian::date(1970, 1, 1));
		Assert(!(posixTime < timeTEpoch));
		if (posixTime < timeTEpoch) {
			return 0;
		}
		const pt::time_duration durationFromTEpoch(posixTime - timeTEpoch);
		return static_cast<time_t>(durationFromTEpoch.total_seconds());
	}

}

void Service::OnUpdate(
			const Trader::Security &security,
			const boost::posix_time::ptime &time,
			Trader::Security::ScaledPrice price,
			Trader::Security::Qty qty,
			bool isBuy) {
	boost::shared_ptr<trader__Trade> update(new trader__Trade);
	const auto date = time - time.time_of_day();
	update->time.date = ConvertPosixTimeToTimeT(date);
	update->time.time = (time - date).total_milliseconds();
	update->param.price = price;
	update->param.qty = qty;
	update->isBuy = isBuy;
	const TradesCacheLock lock(m_tradesCacheMutex);
	auto &cache = m_tradesCache[security.GetSymbol()];
	cache.push_back(update);
}

void Service::GetSecurityList(std::list<trader__Security> &result) {
	std::list<trader__Security> resultTmp;
	foreach (const auto &serviceSecurity, GetNotifyList()) {
		trader__Security security;
		static_assert(
			sizeof(decltype(security.id)) >= sizeof(void *),
			"trader__Security::id too small.");
		typedef decltype(security.id) Id;
		security.id = Id(serviceSecurity.get());
		security.symbol = serviceSecurity->GetSymbol();
		security.scale = serviceSecurity->GetPriceScale();
		resultTmp.push_back(security);
	}
	resultTmp.swap(result);
}

void Service::GetLastTrades(
					const std::string &symbol,
					const std::string &exchange,
					trader__TradeList &result) {
	std::list<trader__Trade> resultTmp;
	std::list<boost::shared_ptr<trader__Trade>> cache;
	if (boost::iequals(exchange, "nasdaq")) {
		const TradesCacheLock lock(m_tradesCacheMutex);
		const auto it = m_tradesCache.find(symbol);
		if (it != m_tradesCache.end()) {
			it->second.swap(cache);
		}
	}
	foreach (const auto &update, cache) {
		resultTmp.push_back(*update);
	}
	resultTmp.swap(result);
}

const Trader::Security & Service::FindSecurity(const std::string &symbol) const {
	foreach (const auto &security, GetNotifyList()) {
		if (boost::iequals(security->GetSymbol(), symbol)) {
			return *security;
		}
	}
	boost::format error("Failed to find security \"%1%\" in notify list");
	error % symbol;
	throw UnknownSecurityError(error.str().c_str());
}

void Service::GetParams(
			const std::string &symbol,
			const std::string &exchange,
			trader__ExchangeParams &result) {
	if (!boost::iequals(exchange, "nasdaq")) {
		result = trader__ExchangeParams();
		return;
	}
	try {
		const Security &security = FindSecurity(symbol);
		result.ask.price = security.GetAskPriceScaled();
		result.ask.qty = security.GetAskQty();
		result.bid.price = security.GetBidPriceScaled();
		result.bid.qty = security.GetBidQty();
	} catch (const UnknownSecurityError &) {
		return;
	}
}

void Service::GetCommonParams(
			const std::string &symbol,
			trader__CommonParams &result) {
	try {
		const Security &security = FindSecurity(symbol);
		result.last.price = security.GetLastPriceScaled();
		result.last.qty = security.GetLastQty();
		result.best.ask.price = security.GetAskPriceScaled();
		result.best.ask.qty = security.GetAskQty();
		result.best.bid.price = security.GetBidPriceScaled();
		result.best.bid.qty = security.GetBidQty();
		result.volumeTraded = security.GetTradedVolume();
	} catch (const UnknownSecurityError &) {
		return;
	}
}
