License terms of the VNF package content.

The project license has not been chosen yet; until then this directory only
holds this note. tools/csar/build.py adds the repository's LICENSE file here
when one exists.

Third-party content of the package:
  * Definitions/etsi_nfv_sol001_*.yaml: ETSI GS NFV-SOL 001 type definitions,
    Copyright ETSI, BSD-3-Clause: ETSI-SOL001-LICENSE.txt (vendored from
    https://forge.etsi.org/rep/nfv/SOL001 by tools/csar/fetch-sol001.sh).
  * The integration chart holds only the project's own subcharts. The
    platform it needs (kube-prometheus-stack, strimzi-kafka-operator) is
    installed separately and is not part of the package.
