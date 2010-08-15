#! /bin/sh

c++ $CXXFLAGS -c $1 -o $2 >&2 || exit $?

nm $2 || exit $?
