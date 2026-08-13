PHP_ARG_ENABLE([carbon],
  [whether to enable CarbonC support],
  [AS_HELP_STRING([--enable-carbon], [Enable CarbonC support])],
  [no])

if test "$PHP_CARBON" != "no"; then
  CARBONC_ROOT="$abs_srcdir/../.."
  PHP_ADD_INCLUDE([$CARBONC_ROOT/include])
  PHP_NEW_EXTENSION([carbon], [carbon_php.c ../../src/carbon.c], [$ext_shared],, [-std=gnu11])
fi
