# Release notes

One file per release, named after its tag (`v<version>.md`, e.g.
`v0.1.0-PZ.md`). The Release workflow (Actions → Release) publishes the file
matching the version it is given, and stops before building when it is missing.

PS5 releases have their own files, `ps5-v<version>.md` (e.g.
`ps5-v0.1.0-PZ.md`). **Actions → PS5 Release** creates the tag `ps5-v<version>`
and a draft release with that file as its description; the title is built
locally (see "PlayStation 5" in the top-level README), so attach its zip to the
draft and then publish it.
