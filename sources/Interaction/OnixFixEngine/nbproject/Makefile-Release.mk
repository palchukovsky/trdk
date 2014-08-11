#
# Generated Makefile - do not edit!
#
# Edit the Makefile in the project folder instead (../Makefile). Each target
# has a -pre and a -post target defined where you can add customized code.
#
# This makefile implements configuration specific macros and targets.


# Environment
MKDIR=mkdir
CP=cp
GREP=grep
NM=nm
CCADMIN=CCadmin
RANLIB=ranlib
CC=gcc
CCC=g++
CXX=g++
FC=gfortran
AS=as

# Macros
CND_PLATFORM=GNU-Linux-x86
CND_DLIB_EXT=so
CND_CONF=Release
CND_DISTDIR=dist
CND_BUILDDIR=build

# Include project Makefile
include Makefile

# Object Directory
OBJECTDIR=${CND_BUILDDIR}/${CND_CONF}/${CND_PLATFORM}

# Object Files
OBJECTFILES= \
	${OBJECTDIR}/Api.o \
	${OBJECTDIR}/CurrenexFixExchange.o \
	${OBJECTDIR}/OnixFixEngine.o


# C Compiler Flags
CFLAGS=

# CC Compiler Flags
CCFLAGS=
CXXFLAGS=

# Fortran Compiler Flags
FFLAGS=

# Assembler Flags
ASFLAGS=

# Link Libraries and Options
LDLIBSOPTIONS=../../Common/dist/Release/GNU-Linux-x86/libcommon.a -Wl,-rpath,../../Core/dist/Release/GNU-Linux-x86 -L../../Core/dist/Release/GNU-Linux-x86 -lCore

# Build Targets
.build-conf: ${BUILD_SUBPROJECTS}
	"${MAKE}"  -f nbproject/Makefile-${CND_CONF}.mk ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT}

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT}: ../../Common/dist/Release/GNU-Linux-x86/libcommon.a

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT}: ../../Core/dist/Release/GNU-Linux-x86/libCore.so

${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT}: ${OBJECTFILES}
	${MKDIR} -p ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}
	${LINK.cc} -o ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT} ${OBJECTFILES} ${LDLIBSOPTIONS} -shared -fPIC

${OBJECTDIR}/Api.o: Api.cpp 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.cc) -O2 -DBOOST_DISABLE_ASSERTS -DNDEBUG -DNTEST -I../.. -I../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include/../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include -std=c++11 -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/Api.o Api.cpp

${OBJECTDIR}/CurrenexFixExchange.o: CurrenexFixExchange.cpp 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.cc) -O2 -DBOOST_DISABLE_ASSERTS -DNDEBUG -DNTEST -I../.. -I../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include/../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include -std=c++11 -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/CurrenexFixExchange.o CurrenexFixExchange.cpp

${OBJECTDIR}/OnixFixEngine.o: OnixFixEngine.cpp 
	${MKDIR} -p ${OBJECTDIR}
	${RM} "$@.d"
	$(COMPILE.cc) -O2 -DBOOST_DISABLE_ASSERTS -DNDEBUG -DNTEST -I../.. -I../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include/../../../externals/OnixS.FixEngineCpp-RHEL52-gcc412-x64-3_15_1_0/include -std=c++11 -fPIC  -MMD -MP -MF "$@.d" -o ${OBJECTDIR}/OnixFixEngine.o OnixFixEngine.cpp

# Subprojects
.build-subprojects:
	cd ../../Common && ${MAKE}  -f Makefile CONF=Release
	cd ../../Core && ${MAKE}  -f Makefile CONF=Release
	cd ../../Common && ${MAKE}  -f Makefile CONF=Release
	cd ../../Core && ${MAKE}  -f Makefile CONF=Release

# Clean Targets
.clean-conf: ${CLEAN_SUBPROJECTS}
	${RM} -r ${CND_BUILDDIR}/${CND_CONF}
	${RM} ${CND_DISTDIR}/${CND_CONF}/${CND_PLATFORM}/libOnixFixEngine.${CND_DLIB_EXT}

# Subprojects
.clean-subprojects:
	cd ../../Common && ${MAKE}  -f Makefile CONF=Release clean
	cd ../../Core && ${MAKE}  -f Makefile CONF=Release clean
	cd ../../Common && ${MAKE}  -f Makefile CONF=Release clean
	cd ../../Core && ${MAKE}  -f Makefile CONF=Release clean

# Enable dependency checking
.dep.inc: .depcheck-impl

include .dep.inc
