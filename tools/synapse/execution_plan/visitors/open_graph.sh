#!/bin/bash

set -e

if [[ $# -eq 0 ]] ; then
  exit 1
fi

TMP_DIR=/tmp

exec_plan_dot_file=$1
exec_plan_graph="${1%.*}".ps2

mime_type="application/postscript"
default_app=$(grep -i ^exec $(locate -n 1 $(xdg-mime query default $mime_type | cut -d';' -f 1)) | perl -pe 's/.*=(\S+).*/$1/')

dot -Tps2 $exec_plan_dot_file -o $exec_plan_graph

if [[ $# -eq 2 ]] ; then
  search_space_dot_file=$2
  search_space_graph="${2%.*}".ps2

  twopi -Tps2 $search_space_dot_file -o $search_space_graph  
  merged_file=$TMP_DIR/out.pdf

  gs -q -sDEVICE=pdfwrite -dNOPAUSE -dBATCH -dSAFER -sOutputFile=$merged_file $exec_plan_graph $search_space_graph > /dev/null
  $default_app $merged_file

  rm $search_space_dot_file
  rm $search_space_graph
  rm $merged_file
else
  $default_app $exec_plan_graph
fi

rm $exec_plan_dot_file
rm $exec_plan_graph

exit 0
