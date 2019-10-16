#!/bin/bash
####### Prepare build env #####################################################
spath=$(pwd)
echo spath: ${spath}

reldir=$(dirname $0)
cd ${reldir}
absdir=$(pwd)

export STAGEDIR=${HOME}/staging
echo "absdir = ${absdir}"
echo "STAGEDIR = ${STAGEDIR}"

export PKG_CONFIG_PATH=${STAGEDIR}/usr/local/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=${STAGEDIR}
export PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
export PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1
export LD_LIBRARY_PATH=${STAGEDIR}/usr/local/lib

if [[ $@ == *'--clean'* ]]; then
	if [ -d "${spath}/build_dep" ]; then
		echo "CLEAN BUILD. We delete build_dep and staging."
		mv "${spath}/build_dep/libav" /tmp
		mv "${spath}/build_dep/libconfig" /tmp
		mv "${spath}/build_dep/libsndfile" /tmp
		mv "${spath}/build_dep/eigen" /tmp
		rm -Rf "${spath}/build_dep"
		mkdir "${spath}/build_dep"
		mv /tmp/libav "${spath}/build_dep"
		mv /tmp/libconfig "${spath}/build_dep"
		mv /tmp/libsndfile "${spath}/build_dep"
		mv /tmp/eigen "${spath}/build_dep"
		find ${STAGEDIR} -name "libav*" -exec rm -r {} +
		find ${STAGEDIR} -name "ffmpeg*" -exec rm -r {} +
		find ${STAGEDIR} -iname "libconfig*" -delete
		find ${STAGEDIR} -iname "*audiocore*" -exec rm -r {} +
		find ${STAGEDIR} -name "libswresample*" -exec rm -r {} +
		find ${STAGEDIR} -name "audiomonitoring_cwrapper.h" -delete
		find ${STAGEDIR} -name "playbackprocessing_cwrapper.h" -delete
		find ${STAGEDIR} -name "*sndfile*" -exec rm -r {} +
		mkdir -p "${HOME}/${STAGEDIR}"
	fi
fi


####### Functions #############################################################
function set_title {
	echo "####### Build $1 ##################################################"
	echo -en "\033]0; $1 \a"
}

mkdir -p build_dep


####### Build eigen ###########################################################
set_title "eigen (audiocore dependency)"
cd "${spath}/build_dep"
repo="eigen"
eigenv="3.3.7"
eigen_dir="${repo}"
if [ ! -d ${repo} ] ; then
	git clone "https://gitlab.com/libeigen/eigen.git"
	cd eigen
	git checkout ${eigenv} -b ${eigenv}
fi


####### Build libconfig #######################################################
set_title "libconfig (audiocore dependency)"
cd "${spath}/build_dep"
repo=libconfig
libconvv="f53e5de4528b"
if [ ! -d "${repo}" ] ; then
	git clone "https://github.com/hyperrealm/libconfig.git"
	cd ${repo}
	git checkout ${libconvv} -b ${libconvv}
else
	cd ${repo}
fi

if [ ! -f ${STAGEDIR}/usr/local/include/libconfig.h ] ; then
	mkdir build
	cd build
	cmake -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Debug .. ||
		{ echo "cmake of ${repo} failed"; exit 1; }

	make install DESTDIR=${STAGEDIR} ||
		{ echo "building ${repo} failed"; exit 1; }
fi

export CMAKE_PREFIX_PATH=${STAGEDIR}/usr/local/lib/cmake/libconfig

####### Build libav ###########################################################
set_title "libav (audiocore dependency)"
cd "${spath}/build_dep"
repo=libav
libavv="n4.2.2"
if [ ! -d "${repo}" ] ; then
	git clone "https://github.com/FFmpeg/FFmpeg.git" ${repo}
	cd ${repo}
	git checkout ${libavv} -b ${libavv}
else
	cd ${repo}
fi

if [ ! -d ${STAGEDIR}/usr/local/include/libavcodec ] ; then
	./configure --disable-programs --disable-doc --disable-stripping \
		--disable-avdevice --disable-avformat --enable-swresample \
		--disable-swscale --disable-postproc --disable-avfilter \
		--disable-network --disable-dct --disable-dwt \
		--disable-error-resilience --disable-lsp --disable-lzo --disable-mdct \
		--enable-rdft --disable-faan --enable-fft --disable-pixelutils \
		--disable-everything --enable-shared

	make install DESTDIR=${STAGEDIR} ||
		{ echo "building ${repo} failed"; exit 1; }

fi

####### Build libsndfile ######################################################
set_title "libsndfile (audiocore dependency)"
cd "${spath}/build_dep"
repo=libsndfile
sndfilev="1.0.25"
sndfile_f="libsndfile-${sndfilev}"
sndfile_tar="${sndfile_f}.tar.gz"
if [ ! -d "${repo}" ] ; then
	wget "http://www.mega-nerd.com/libsndfile/files/${sndfile_tar}"
	tar xf ${sndfile_tar}
	mv ${sndfile_f} "${repo}"
fi

if [ ! -f ${STAGEDIR}/usr/local/include/sndfile.h ] ; then
	cd ${repo}

	make distclean
	./configure

	make install DESTDIR=${STAGEDIR} ||
		{ echo "building ${repo} failed"; exit 1; }
fi

####### Build audiocore #######################################################
set_title "audiocore"
cd "${spath}/build_dep"
repo=audiocore
#audiocv="v0.8.17"
audiocv="master"
sdir=${repo}_${audiocv}
bdir=build-sh
if [ ! -d "${sdir}" ] ; then
	git clone ssh://git@source.commend.com/aud/audiocore_symphony.git ${sdir}
	cd ${sdir}
	git checkout ${audiocv}
else
	cd ${sdir}
fi

echo "######################################"
echo "building ${sdir} as shared library ..."
echo "######################################"
mkdir -p ${bdir}
cd ${bdir}
cmake \
	-DAUDIOCORE_FS_LIST="8000;16000" -DAUDIOCORE_NMICS_LIST=1 \
	-DEIGEN3_INCLUDE_DIR=${spath}/build_dep/${eigen_dir} \
	-DUSER_INCLUDE_PATH=${STAGEDIR}/usr/local/include \
	-DUSER_LINK_PATH=${STAGEDIR}/usr/local/lib \
	-DCMAKE_CXX_FLAGS=-I${STAGEDIR}/usr/include \
	-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN \
	-DDEFAULT_AUDIOCORE_CONFIG_FILE=../config/AudioCore_default.cfg \
	-DDEFAULT_SPDLOG_CONFIG_FILE=${spath}/build_dep/${sdir}/${bdir}/generated/spdlog.cfg \
	-DCMAKE_BUILD_TYPE=Release .. ||
	{ echo "cmake of ${sdir}/${bdir} failed"; exit 1; }
make -j4 install DESTDIR=${STAGEDIR} ||

	{ echo "building ${sdir}/${bdir} failed"; exit 1; }

echo "BUILD successful"
echo "Export this environemnt!"
echo "LD_LIBRARY_PATH=${STAGEDIR}/usr/local/lib"
