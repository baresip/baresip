cmake_minimum_required(VERSION 3.5)

project(BaresipTests LANGUAGES C)

set(testcase_sources
    ${BARESIP_ROOT_DIR}/test/account.c
    ${BARESIP_ROOT_DIR}/test/aulevel.c
    ${BARESIP_ROOT_DIR}/test/call.c
    ${BARESIP_ROOT_DIR}/test/cmd.c
    ${BARESIP_ROOT_DIR}/test/contact.c
    ${BARESIP_ROOT_DIR}/test/event.c
    ${BARESIP_ROOT_DIR}/test/message.c
    ${BARESIP_ROOT_DIR}/test/net.c
    ${BARESIP_ROOT_DIR}/test/play.c
    ${BARESIP_ROOT_DIR}/test/ua.c
    ${BARESIP_ROOT_DIR}/test/video.c)

#
# Mocks
#
list(
  APPEND
  testcase_sources
  ${BARESIP_ROOT_DIR}/test/mock/dnssrv.c
  ${BARESIP_ROOT_DIR}/test/sip/aor.c
  ${BARESIP_ROOT_DIR}/test/sip/auth.c
  ${BARESIP_ROOT_DIR}/test/sip/domain.c
  ${BARESIP_ROOT_DIR}/test/sip/location.c
  ${BARESIP_ROOT_DIR}/test/sip/sipsrv.c
  ${BARESIP_ROOT_DIR}/test/sip/user.c
  ${BARESIP_ROOT_DIR}/test/mock/cert.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_aucodec.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_aufilt.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_auplay.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_ausrc.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_menc.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_mnat.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_vidsrc.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_vidcodec.c
  ${BARESIP_ROOT_DIR}/test/mock/mock_vidisp.c)

add_executable(test_baresip ${BARESIP_ROOT_DIR}/test/test.c ${BARESIP_ROOT_DIR}/test/main.c ${testcase_sources})
target_link_libraries(
  test_baresip PRIVATE baresip re rem m ${OPENSSL_CRYPTO_LIBRARY}
                       ${OPENSSL_SSL_LIBRARY})

add_test(NAME test_baresip COMMAND test_baresip)

install(TARGETS test_baresip RUNTIME DESTINATION bin/tests)
