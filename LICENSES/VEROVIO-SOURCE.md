# Verovio 6.3.0 source and replacement information

VArpeggio bundles the unmodified Verovio 6.3.0 Python binding
(`verovio/_verovio.abi3.so`) and its Python/data files. Verovio is licensed
under LGPL-3.0-only. The complete GNU GPLv3 and LGPLv3 texts are included in
`tools/python/verovio-6.3.0.dist-info/licenses/` and installed beside this
notice.

The exact corresponding source for the bundled version is available here:

- Release: <https://github.com/rism-digital/verovio/releases/tag/version-6.3.0>
- Source archive:
  <https://github.com/rism-digital/verovio/archive/refs/tags/version-6.3.0.tar.gz>

VArpeggio invokes Verovio through its ordinary public Python interface. The
library remains a separate, replaceable module under the application's private
runtime directory. A user may rebuild Verovio 6.3.0 (or a compatible modified
version) from the source above and replace the installed `verovio` package.
No signature check, technical restriction, or application-specific patch
prevents that replacement.

For as long as VArpeggio distributes this Verovio binary, the VArpeggio
maintainer will also provide the corresponding Verovio 6.3.0 source on request
at no charge beyond the reasonable cost of physical transfer. Requests may be
opened through the VArpeggio repository's GitHub issue tracker.
