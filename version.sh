#! /bin/sh

strip_echo () {
    echo -n $(echo -n "$@" | tr -d '\n')
}

echo_maybe () {
    if test -n "$2"; then
        strip_echo "$1$2"
        exit 1
    fi
}

# Make sure we're in an useful dir (for out-of-tree builds)
cd "$(dirname $0)"

echo_maybe ''      "$(cat .version 2>/dev/null)"
echo_maybe ''      "$(awk -F '[(< |)]+' '/define FFMS_VERSION/ { printf "%d.%d.%d%s", $3, $5, $7, ($9 ? sprintf(".%d", $9) : "") }' include/ffms.h)"
echo_maybe ''      "$(perl -ne '/define FFMS_VERSION.*?(\d+) << 24.*?(\d+) << 16.*?(\d+) << 8.*?(\d+)/ && print "$1.$2.$3". ($4 ? ".$4" : "")' include/ffms.h)"
# All of the following break pkg-config version checking!
# But if we get to this point we can't output anything "proper" anymore so at least try to be useful
echo_maybe 'r'     "$(git log HEAD~1.. 2>/dev/null | grep git-svn-id | cut -d@ -f2 | cut -d' ' -f1)"
echo_maybe 'git-r' "$(git log HEAD~1.. 2>/dev/null | sed 1q | cut -d' ' -f2 | head -c7)"
# While git returns no output when not on a git repository,
# svnversion still does, so run it last
echo_maybe 'r'     "$(svnversion 2>/dev/null)"
echo_maybe ''      "unknown"
