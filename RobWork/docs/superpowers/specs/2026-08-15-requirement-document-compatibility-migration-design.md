# Requirement Document Compatibility Migration

## Goal

Open historical `engineering-requirements.main` documents that contain a legacy
`extensions.frozenArtifact` without weakening the generic extension-conflict
checks. Saving the project must produce one canonical top-level
`frozenArtifact` and must not recreate the legacy duplicate.

## Migration Contract

Migration runs on the complete requirement document before
`RequirementSetJson::fromObject` parses the editable requirement set.

- Top-level `frozenArtifact` is the canonical document field.
- `extensions.frozenArtifact` is a legacy envelope field and is removed from
  requirement extensions.
- When both fields exist, the top-level value wins. A migration warning records
  that the legacy duplicate was discarded.
- When only the legacy extension exists, it is promoted to the top level.
- The selected artifact still passes the existing JSON and
  `RequirementFreezer::isCurrent` validation. Invalid or stale evidence leaves
  the requirements editable and requires refreezing.
- Other top-level/explicit-extension conflicts remain strict errors.

## Persistence

The serializer removes `frozenArtifact` from requirement extensions before
writing and writes frozen evidence only at the document top level. A successful
compatibility migration marks the project document dirty until the normal
project save transaction persists the canonical form.

## User Feedback

Successful migration keeps the resource loaded and appends an actionable status
message asking the user to save the project. The project-open warning must no
longer report that the optional requirements resource was skipped.

## Tests

Tests cover top-level-only, legacy-extension-only, identical duplicates,
different duplicates, strict unrelated extension conflicts, canonical save
output, migration dirty state, and save/load idempotence.
