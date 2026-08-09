# Plugin UI English Copy Design

## Goal

Replace all Chinese user-facing text in the Engineering Requirements and Structure Optimizer plugins with concise English. Revise the English text already present in those two plugin interfaces so a robotics engineer can identify each command, setting, and state at a glance.

## Scope

- `engineeringrequirements` and `structureoptimizer` user-visible Qt widget text only.
- Tabs, labels, buttons, combo-box entries, dialog text, status messages, progress text, and tooltips.
- Existing tests that assert display text or inspect UI controls.

No object names, domain data, serialization, file formats, algorithms, or plugin workflows change.

## Copy System

- Use compact, concrete labels. Commands use a verb plus object, such as `Import Stations`, `Freeze Requirements`, and `Preview Candidate`.
- Preserve established engineering terms: `TCP`, `WorkCell`, `Quick`, `Verified`, `Candidate`, and `Frozen Requirements`.
- Prefer nouns for tabs and sections. The Engineering Requirements tabs are `Key Stations`, `Workspace Regions`, and `Validate & Freeze`. The Structure Optimizer tabs are `Design Variables`, `Tasks & Constraints`, `Optimization Settings`, `Candidates`, and `Export Report`.
- Status messages state the current result or action in one sentence. They do not restate process history unless it changes the engineer's next action.
- Tooltips describe the action and the relevant constraint or input format only.

## Implementation

1. Add focused UI-copy regression checks before changing display strings, then run them to demonstrate the pre-change mismatch.
2. Replace the user-facing strings in each plugin widget and its presentation helpers. Keep all internal identifiers and data values unchanged.
3. Update the regression expectations and run the affected test targets and static scans to verify no Chinese UI text remains in the two plugin source directories.

## Validation

- Build and run the Engineering Requirements and Structure Optimizer test targets.
- Run a source scan for CJK characters in the two plugin source directories, allowing only non-UI documentation where applicable.
- Run `git diff --check` to reject whitespace errors.
