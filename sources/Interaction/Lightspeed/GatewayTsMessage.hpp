/**************************************************************************
 *   Created: 2012/08/28 23:28:05
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "GatewayMessage.hpp"

namespace Trader {  namespace Interaction { namespace Lightspeed {

	template<typename BufferT>
	class GatewayTsMessage : public GatewayMessage {

	public:

		enum Type {
			TYPE_DEBUG			= '+',
			TYPE_LOGIN_ACCEPTED	= 'A',
			TYPE_LOGIN_REJECTED	= 'J'
		};

		typedef BufferT Buffer;
		typedef typename Buffer::const_iterator Iterator;

	public:

		class Error : public GatewayMessage::Error {
		public:
			explicit Error(const char *what, Iterator messageBegin, Iterator messageEnd)
					: GatewayMessage::Error(what),
					m_buffer(messageBegin, messageEnd) {
				//...//
			}
			virtual ~Error() {
				//...//
			}
		public:
			virtual const std::string & GetSubject() const {
				return m_buffer;
			}
		private:
			std::string m_buffer;
		};
	
		class MessageNotGatawayMessageError : public Error {
		public:
			MessageNotGatawayMessageError(Iterator messageBegin, Iterator messageEnd)
					: Error(
						"Message not Lightspeed Gateway trade system message",
						messageBegin,
						messageEnd) {
				//...//
			}
		};

		class FieldDoesntExistError : public Error {
		public:
			FieldDoesntExistError(Iterator messageBegin, Iterator messageEnd)
					: Error(
						"Field doesn't exist in Lightspeed Gateway trade system message",
						messageBegin,
						messageEnd) {
				//...//
			}
		};
		
		class FieldHasInvalidFormatError : public Error {
		public:
			FieldHasInvalidFormatError(Iterator messageBegin, Iterator messageEnd)
					: Error(
						"Lightspeed Gateway trade system message field has invalid format",
						messageBegin,
						messageEnd) {
				//...//
			}
		};

	public:

		GatewayTsMessage(Iterator messageBegin, Iterator messageEnd)
				: m_messageBegin(messageBegin),
				m_messageEnd(messageEnd),
				m_len(std::distance(messageBegin, messageEnd)) {
			if (!m_len) {
				throw MessageNotGatawayMessageError(m_messageBegin, m_messageEnd);
			}
			switch (*m_messageBegin) {
				case GatewayTsMessage::TYPE_DEBUG:
				case GatewayTsMessage::TYPE_LOGIN_ACCEPTED:
				case GatewayTsMessage::TYPE_LOGIN_REJECTED:
					m_isLightspeedMessage = false;
					m_type = Type(*m_messageBegin);
					break;
				default:
					if (m_len < m_timestampFieldSize + 1) {
						throw MessageNotGatawayMessageError(m_messageBegin, m_messageEnd);
					}
					{
						const auto begin = m_messageBegin + m_timestampFieldSize;
//						switch (*begin) {
// 							case GatewayTsMessage::TYPE_LOGIN_ACCEPTED:
// 								m_isLightspeedMessage = true;
// 								m_type = Type(*begin);
// 								break;
//							default:
								throw MessageNotGatawayMessageError(m_messageBegin, m_messageEnd);
//						}
					}
					break;
			}
		}

	public:

		Iterator GetBegin() const {
			return m_messageBegin;
		}

		Iterator GetEnd() const {
			return m_messageEnd;
		}

	public:

		Type GetType() const {
			return m_type;
		}
		
		std::string GetAsString() const {
			const auto begin = !m_isLightspeedMessage
				?	m_messageBegin
				:	m_messageBegin + m_timestampFieldSize;
			std::string result(begin + 1, m_messageEnd);
			boost::trim(result);
			return result;
		}

		Char GetCharField(FieldStart offset) const {
			if (m_len < offset + 1) {
				throw FieldHasInvalidFormatError(m_messageBegin, m_messageEnd);
			}
			return *(m_messageBegin + offset);
		}

		std::string GetAlphanumField(FieldStart offset, Len len) const {
			std::string result;
			GetField(offset, len, result);
			return result;
		}

		Numeric GetNumericField(FieldStart offset, Len len) const {
			std::string strVal;
			GetField(offset, len, strVal);
			return boost::lexical_cast<Numeric>(strVal);
		}

	private:

		void GetField(FieldStart offset, Len len, std::string &result) const {
			if (m_len < offset + len) {
				throw FieldHasInvalidFormatError(m_messageBegin, m_messageEnd);
			}
			const auto fieldBegin = m_messageBegin + offset;
			const auto fieldEnd = fieldBegin + len;
			std::string resultTmp(fieldBegin, fieldEnd);
			boost::trim(resultTmp);
			resultTmp.swap(result);
		}


	private:

		Iterator m_messageBegin;
		Iterator m_messageEnd;
		Len m_len;
		bool m_isLightspeedMessage;
	
		Type m_type;

		static const Len m_timestampFieldSize;

	};

	template<typename BufferT>
	const typename GatewayTsMessage<BufferT>::Len GatewayTsMessage<BufferT>::m_timestampFieldSize = 8;

} } }
