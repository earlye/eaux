# eaux

"Eaux" is French for "waters". Here I'm thinking of it as a collection
of code to help navigate the world of writing other code. So functions
that I always find myself writing or trying to find. So you can think
of it as a set of utilities, a commons, or whatever.

## Repo layout

I want things to be as decoupled as possible, while also keeping it as
a monorepo. So the top-level directory will be split on language boundary,
and then inside each will be a forest of topics and micro-libraries:

{language}/{topic}/{library}

For example:

go/paths/UserDirectory/
rust/ipc/app-agent/
c++/text/static_format/

Run `make` at the root to build and test every language tree (`go/`,
`rust/` and `c++/`). Each library has its own README and MIT license, and
its own Makefile except in `c++/`, where one Makefile drives a single CMake
build of the whole tree.
