#
# CMakeLists.txt
#
# Copyright (C) 2022 Sebastian Reimers
#

project(selftest)

include_directories(${RE_INCLUDE_DIRS} ${BARESIP_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}
  account.c
  call.c
  cmd.c
  contact.c
  event.c
  jbuf.c
  menu.c
  message.c
  net.c
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
