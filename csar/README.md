Sources of the ETSI NFV SOL004 package (CSAR), assembled by
`tools/csar/build.py` and checked by `tools/csar/validate.py`; see
`doc/packaging.adoc`.

* `TOSCA-Metadata/TOSCA.meta.tpl`, `Definitions/vnfd.yaml.tpl`: templates
  (`${VAR}` placeholders filled from project.env, VERSION and the build).
* `Definitions/etsi_nfv_sol001_*.yaml`: vendored ETSI type definitions
  (`tools/csar/fetch-sol001.sh`; version in `Definitions/SOL001_VERSION`).
* `Files/`: change log, licenses, test descriptions; copied verbatim.
