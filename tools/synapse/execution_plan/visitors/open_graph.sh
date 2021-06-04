#!/bin/bash

set -e

if [[ $# -eq 0 ]] ; then
  exit 1
fi

file_ext="svg"

generated=()

function generate {
  if=$1
  of="${if%.*}".$file_ext

  dot -T$file_ext $if -o $of
  generated+=("$of")
}

function open {
  f=$1
  xdg-open $f & > /dev/null
}

for f in "${@}"
do
  generate "$f"
done

#if [ "${#generated[@]}" -eq 1 ]; then
#  open "${generated[0]}"
#else
#  merged_file=/tmp/out.pdf
#  gs -q -sDEVICE=pdfwrite -dNOPAUSE -dBATCH -dSAFER -sOutputFile=$merged_file "${generated[@]/#/}" > /dev/null
#  open $merged_file
#  rm $merged_file
#fi

for of in "${generated[@]}"
do
  open $of
  #rm $of 2> /dev/null
done

exit 0
