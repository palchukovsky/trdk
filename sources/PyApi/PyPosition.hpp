/**************************************************************************
 *   Created: 2013/01/18 03:06:02
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/Position.hpp"

namespace trdk { namespace PyApi {

	template<typename ImplT>
	class SidePosition : public ImplT {

	public:

		typedef ImplT Impl;

	private:

		typedef SidePositionExport<SidePosition<Impl>> Export;

	public:

		explicit SidePosition(
					trdk::Strategy &strategy,
					trdk::Security &security,
					Qty qty,
					ScaledPrice startPrice,
					SidePositionExport<SidePosition<Impl>> &positionExport)
				: Impl(strategy, security, qty, startPrice),
				m_positionExport(positionExport) {
			//....//
		}

	public:

		void TakeExportObjectOwnership() {
			Assert(!m_positionExportRefHolder);
			m_positionExportRefHolder = m_positionExport.shared_from_this();
			m_positionExport.MoveRefToCore();
			m_positionExport.ResetRefHolder();
		}
		
		SidePositionExport<SidePosition<Impl>> & GetExport() {
			Assert(m_positionExportRefHolder);
			return m_positionExport;
		}
		const SidePositionExport<SidePosition<Impl>> & GetExport() const {
			return const_cast<SidePosition *>(this)->GetExport();
		}

	private:

		Export &m_positionExport;
		boost::shared_ptr<Export> m_positionExportRefHolder;

	};

	typedef SidePosition<trdk::LongPosition> LongPosition;
	typedef SidePosition<trdk::ShortPosition> ShortPosition;

} }