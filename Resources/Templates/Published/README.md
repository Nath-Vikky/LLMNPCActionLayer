# Published Template Sources

This directory contains canonical JSON snapshots for templates reviewed and
published while developing the editable project plugin.

- Runtime loads cooked `ULLMNPCMotionTemplate` assets, not these JSON files.
- The JSON is version-control and audit evidence for the corresponding asset.
- Transient local draft paths are removed during export.
- Licensed external Animation Assets may remain in the host project. Their
  path, package fingerprint, license, and review evidence stay in the JSON,
  while raw assets marked as non-redistributable are not copied here.
- When the plugin is installed or read-only, Workbench publication falls back
  to `<Project>/LLMNPCSource/Templates/Published`.
