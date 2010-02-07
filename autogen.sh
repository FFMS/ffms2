#! /bin/sh
AUTOMAKE="automake --foreign"; export AUTOMAKE
ACLOCAL="aclocal -I m4"; export ACLOCAL
autoreconf -f -i
