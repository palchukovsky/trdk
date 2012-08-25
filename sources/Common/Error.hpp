/**************************************************************************
 *   Created: 2008/10/23 10:28
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: TunnelEx
 *       URL: http://tunnelex.net
 **************************************************************************/

#pragma once

#include <string>

class Error {

public:

	explicit Error(int errorNo) throw();
	~Error() throw();

public:

	std::wstring GetStringW() const;
	std::string GetString() const;
	int GetErrorNo() const;

	bool IsError() const;

	//! Returns true if error could be resolved to string.
	bool CheckError() const;

private:

	int m_errorNo;

};

std::ostream & operator <<(std::ostream &os, const Error &);
std::wostream & operator <<(std::wostream &os, const Error &);
