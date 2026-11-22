# SHA256-locked Python 2-compatible wheels for the pinned uncompyle6 submodule.
# Source: https://pypi.org/project/<package>/<version>/ -> "py2" wheel.
# Do not replace these with current Py3-only releases or unpinned pip installs.
# The uncompyle6 git submodule requires spark-parser >=1.8.9,<1.9.2 and
# xdis >=6.1.1,<6.2.0. Click and six are their transitively required packages.
#
# Entries: name|release wheel filename|SHA256|PyPI CDN URL
set(EVECC_WHEELS
  "xdis|xdis-6.1.1-py2-none-any.whl|0f7bf67cd9c3a2fa3b14857f05ee159308e4aec97168bbf3cc8db902a14a0e24|https://files.pythonhosted.org/packages/aa/28/59714b850faaac29cfd86d5874a42f8fdbc7d3bffb29c0a1f7496c89b4e1/xdis-6.1.1-py2-none-any.whl"
  "spark_parser|spark_parser-1.9.0-py2-none-any.whl|fb17b1dc56de2d2554ae2c95705b2a5940a04b3a0aa43dcdf2fd609ad3edbf22|https://files.pythonhosted.org/packages/32/49/9ffca7bd17648e6bcc3371960abf4aaf53e5fe1249bdef9bc8c7ae23fd9c/spark_parser-1.9.0-py2-none-any.whl"
  "click|click-7.1.2-py2.py3-none-any.whl|dacca89f4bfadd5de3d7489b7c8a566eee0d3676333fbb50030263894c38c0dc|https://files.pythonhosted.org/packages/d2/3d/fa76db83bf75c4f8d338c2fd15c8d33fdd7ad23a9b5e57eb6c5de26b430e/click-7.1.2-py2.py3-none-any.whl"
  "six|six-1.16.0-py2.py3-none-any.whl|8abb2f1d86890a2dfb989f9a77cfcfd3e47c2a354b01111771326f8aa26e0254|https://files.pythonhosted.org/packages/d9/5a/e7c31adbe875f2abbb91bd84cf2dc52d792b5a01506781dbcf25c91daf11/six-1.16.0-py2.py3-none-any.whl"
)
