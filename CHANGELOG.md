# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-08-11

### Changed
- Move to version `v0.7.5` of `ncarray`
- Move to version `v0.1.4` of `sbio`

### Fixed
- Fixed the relative path provided in `.pc` files bundled inside wheels. `${prefix}` still likely has no useful purpose, but `Cflags` and `Libs` can now be used directly.


## [0.1.0] - 2026/08/06
- Initial release
