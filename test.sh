#!/bin/bash

strcnt=1
fail=0
pass=0

for gfile in `ls -v examples/*.ebnf` ; do
    ./genrec $gfile "examples/string$strcnt" -c >"examples/$strcnt.output" 2>/dev/null
    if [ "$?" = "0" ] && cmp -s "examples/$strcnt.output" "examples/$strcnt.expect" ; then
        echo "==> Grammar: $gfile, String: string$strcnt [PASS]"
        let pass=pass+1
    else
        echo "==> Grammar: $gfile, String: string$strcnt [FAIL]"
        let fail=fail+1
    fi
    let strcnt=strcnt+1
done

echo "Pass: $pass, Fail: $fail"
