#
# CMakeLists.txt
#
# Copyright (C) 2022 Sebastian Reimers
#

project(selftest)

include_directories(${RE_INCLUDE_DIRS} ${BARESIP_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}
  account.c
  ausrc.c
  bevent.c
  bundle_publication.c
  call.c
  call_cancelrule.c
  call_fixture.c
  cmd.c
  contact.c
  cparam.c
  dial_number.c
  jbuf.c
  jbuf_gnack.c
  message.c
  media_transport.c
  mnat.c
  net.c
  peerconn.c
  peerconn_transport.c
  play.c
  stunuri.c
  ua.c
  video.c

  mock/dnssrv.c

  sip/aor.c
  sip/auth.c
  sip/domain.c
  sip/location.c
  sip/sipsrv.c
  sip/user.c

  mock/cert.c

  mock/mock_aucodec.c
  mock/mock_auplay.c
  mock/mock_mnat.c
  mock/mock_vidcodec.c
  mock/mock_vidisp.c

  test.c

  main.c
)

target_link_libraries(${PROJECT_NAME} baresip ${REM_LIBRARIES} ${RE_LIBRARIES})

# Transport selftests load this module dynamically.  Keep a targeted selftest
# rebuild from exercising a stale module after transport API changes.
if(TARGET dtls_srtp)
  add_dependencies(${PROJECT_NAME} dtls_srtp)
endif()
