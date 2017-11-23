
#pragma once

#define TRDK_VERSION_RELEASE	1
#define TRDK_VERSION_BUILD	0
#define TRDK_VERSION_STATUS	11

#define TRDK_VERSION_BRANCH		"WilliamCarry"
#define TRDK_VERSION_BRANCH_W	L"WilliamCarry"

#define TRDK_VENDOR		"Eugene V. Palchukovsky"
#define TRDK_VENDOR_W	L"Eugene V. Palchukovsky"

#define TRDK_DOMAIN		"robotdk.com"
#define TRDK_DOMAIN_W	L"robotdk.com"

#define TRDK_SUPPORT_EMAIL		"support@robotdk.com"
#define TRDK_SUPPORT_EMAIL_W	L"support@robotdk.com"

#define TRDK_LICENSE_SERVICE_SUBDOMAIN		"licensing"
#define TRDK_LICENSE_SERVICE_SUBDOMAIN_W	L"licensing"

#define TRDK_NAME	"Multibroker"
#define TRDK_NAME_W	L"Multibroker"

#define TRDK_COPYRIGHT		"Copyright 2017 (C) Eugene V. Palchukovsky, robotdk.com. All rights reserved."
#define TRDK_COPYRIGHT_W	L"Copyright 2017 (C) Eugene V. Palchukovsky, robotdk.com. All rights reserved."

#define TRDK_CONCURRENCY_PROFILE_DEBUG (::trdk::Lib::Concurrency::PROFILE_RELAX)
#define TRDK_CONCURRENCY_PROFILE_TEST (::trdk::Lib::Concurrency::PROFILE_RELAX)
#define TRDK_CONCURRENCY_PROFILE_RELEASE (::trdk::Lib::Concurrency::PROFILE_RELAX)

#define TRDK_GET_REQUIRED_MODUE_FILE_NAME_LIST() {"Core", "Engine", "FrontEnd", "Shell", "FixProtocol", "WilliamCarry", };

