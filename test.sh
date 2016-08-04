#!/bin/bash

strcnt=1
fail=0
pass=0

for gfile in `ls -v examples/*.ebnf` ; do
    echo "==> Grammar: $gfile, String: string$strcnt"
    ./genrec $gfile "examples/string$strcnt" -c >/dev/null
    if [ "$?" = "0" ] ; then
        let pass=pass+1
    else
        let fail=fail+1
    fi
    let strcnt=strcnt+1
done

echo "Pass: $pass, Fail: $fail"
