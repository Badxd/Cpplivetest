# Library revision under test

- Library: CppGram 1.0.0 release candidate, reconciled source. The base is the attached snapshot (b3b48b3). The fixes
  listed in `CHANGELOG.md` in the library archive, section 1.0.0 are applied on top of it.
- Revision identifier: the runner prints the version and a tree digest in its header. The digest is a SHA256 over the
  sorted relative paths and SHA256 values of every file in `include/` and `src/` of the CppGram tree. CMake computes it
  when this project configures with `-DCPPGRAM_SOURCE_DIR`.
- Digest of the tree this release was built from: `57e313ea306c76c3f2de6cc97e6275b9e6664c1f53f3c4fb53daf409037ad58d`.
- To check a copy: configure this project against that tree and compare the `library tree` line in the runner header.
- The digest covers the library source only. Test, doc and tool changes do not change it.
- The runner was built against a prebuilt `libcppgram.a` from the same tree. Its own live checks use the public headers
  only.
