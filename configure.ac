AC_INIT([dms], [0.1.5], [dev+ubuntu@pinpayments.com], [], [https://github.com/pinpayments/dms])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC_C99
AM_PROG_AS
AC_CONFIG_HEADERS([src/config.h])
AC_PROG_RANLIB
AC_CONFIG_FILES([
  Makefile
  src/Makefile
])
AC_CHECK_LIB(curl, [curl_easy_init curl_multi_timeout])
AC_CHECK_LIB(jansson, json_loads)
AC_OUTPUT
