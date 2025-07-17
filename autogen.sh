#! /bin/sh
mkdir -p src/config
echo Running autoreconf...
autoreconf -ivf
if [ -z "$NOCONFIGURE" ]; then
    echo Running configure...
    ./configure "$@"
fi
