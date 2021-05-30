#!/bin/bash

set -e

if [[ $# -eq 0 ]] ; then
  exit 1
fi

generated=()

function generate_ps2 {
  if=$1
  of="${if%.*}".ps2

  dot -Tps2 $if -o $of
  generated+=("$of")
}

function open_ps2 {
  f=$1
  mime_type="application/postscript"
  default_app=$(grep -i ^exec $(locate -n 1 $(xdg-mime query default $mime_type | cut -d';' -f 1)) | perl -pe 's/.*=(\S+).*/$1/')
  $default_app $f > /dev/null
}

for f in "${@}"
do
  generate_ps2 "$f"
done

if [ "${#generated[@]}" -eq 1 ]; then
  open_ps2 "${generated[0]}"
else
  merged_file=/tmp/out.pdf
  gs -q -sDEVICE=pdfwrite -dNOPAUSE -dBATCH -dSAFER -sOutputFile=$merged_file "${generated[@]/#/}" > /dev/null
  open_ps2 $merged_file
  rm $merged_file
fi

for of in "${generated[@]}"
do
  rm $of 2> /dev/null
done

exit 0
