#!/bin/sh

apt-get update \
&& apt-get -y install --no-install-recommends \
    build-essential \
    libdb-dev \
    libsqlite3-dev \
    libssl-dev \
    flex \
    bison \
    libjansson-dev \
    libmosquitto-dev \
    libev-dev \
    pkg-config \
    git \
    sqlite3 \
    dpkg-dev \
    debhelper-compat \
    default-libmysqlclient-dev \
    gperf \
    libxtables-dev libip6tc-dev libip4tc-dev \
    libavcodec-dev \
    libavfilter-dev \
    libavformat-dev \
    libavutil-dev \
    libbencode-perl \
    libcrypt-openssl-rsa-perl \
    libcrypt-rijndael-perl \
    libcurl4-openssl-dev \
    libdigest-crc-perl \
    libdigest-hmac-perl \
    libevent-dev libglib2.0-dev \
    libhiredis-dev libio-multiplex-perl \
    libio-socket-inet6-perl libiptc-dev \
    libjson-glib-dev libjson-c-dev libnet-interface-perl \
    libpcap0.8-dev \
    libpcre3-dev \
    libsocket6-perl \
    libspandsp-dev \
    libswresample-dev \
    libsystemd-dev \
    libxmlrpc-core-c3-dev \
    libwebsockets-dev \
    libbcg729-dev \
    libluajit-5.1-dev \
    markdown \
    zlib1g-dev \
    dh-sequence-dkms \
    libjson-perl \
    libmnl-dev \
    libncurses-dev \
    libnftnl-dev \
    libopus-dev \
    libtest2-suite-perl \
    pandoc \
    cmake \
    libsctp1 \
    uuid-runtime \
    libsctp-dev \
    libpcap-dev \
    libcurl4-openssl-dev \
    libgsl-dev \
    libtool  \
    libasound2-dev libasound2 libasound2-data \
    libsndfile1-dev \
    libgstreamer-plugins-base1.0-0 libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-0 libgstreamer1.0-dev \
    libxext-dev \
    libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev \
    libgsm1-dev libtiff-dev libvpx-dev libx265-dev \
    software-properties-common \
    libjansson4 libpcre3 libncursesw6 libgsl27 openssl libmosquitto1 \
    libsqlite3-0 sysvinit-utils lsb-base libavcodec59 libavformat59 libavutil57 \
    libbcg729-0 libc6 libevent-2.1-7 libevent-pthreads-2.1-7 libglib2.0-0 \
    libhiredis0.14 libip4tc2 libip6tc2 iptables libmariadb3 libjson-glib-1.0-0 \
    libopus0 libpcap0.8 libspandsp2 libssl3 libswresample4  libsystemd0 \
    libwebsockets17 libxmlrpc-core-c3 zlib1g libluajit-5.1-2 \
    iproute2 procps \
    libhttp-tiny-perl libjson-maybexs-perl libmoose-perl \
    libdatetime-format-iso8601-perl libmoosex-classattribute-perl \
    liburl-encode-perl libwww-perl libthrowable-perl libproc-processtable-perl \
    liburi-template-perl libdata-uuid-perl libobject-event-perl \
    libanyevent-perl libmodule-pluggable-perl libnet-dns-perl \
    libtemplate-perl libconfig-ini-perl libfile-homedir-perl libipc-system-simple-perl \
    liblog-any-adapter-tap-perl libnet-dns-native-perl libanyevent-handle-udp-perl \
    libmime-types-perl libjson-path-perl libxml-xpath-perl \
    libbytes-random-secure-perl libjson-maybexs-perl libsyntax-keyword-try-perl \
    libtest-checkdeps-perl libtest-deep-perl libtest-fatal-perl libtest-files-perl \
    libtest-refcount-perl libtest-warnings-perl libunicode-utf8-perl \
    libnetaddr-ip-perl libindirect-perl \
    libanyevent-fork-perl libguard-perl \
    libtemplate-perl libnet-address-ip-local-perl \
    libxml-simple-perl libfile-slurp-perl libdata-structure-util-perl \
    libnet-dbus-perl libhash-moreutils-perl liblist-moreutils-perl \
    libtie-hash-expire-perl \
    libpulse-dev libpng-dev \
    nodejs ghostscript wget \
    golang-go libjwt-dev

git clone ${GIT_CRED_URL}/sipfront/libg7221.git && \
    cd libg7221 && ./autogen.sh && ./configure && make && make install

apt install -y libavcodec-dev libavcodec-extra libavfilter-dev libavfilter-extra libavformat-dev libavformat59 libswscale-dev libswscale6 libavdevice-dev libavdevice59 libavutil-dev libavutil57

git clone -b v3.23.0-Sipfront ${GIT_CRED_URL}/sipfront/re.git && \
    cd re && \
    cmake -B build && \
    cmake --build build -j && \
    cmake --install build

cd /src && \
    rm -rf build out/* && \
    cmake -B build && \
    cmake --build build -j && \
    cmake --install build --prefix /src/out

