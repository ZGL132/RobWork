# Pose Reachability Smoke Checklist

Date: 2026-07-18
Build: `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug`
Executable: `build/Desktop_Qt_6_11_1_MSVC2022_64bit-Debug/RobWorkStudio/bin/RobWorkStudio.exe`
WorkCell: `GenericSixAxis.wc.xml`

## Launch

Command:

```powershell
& "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\Desktop_Qt_6_11_1_MSVC2022_64bit-Debug\RobWorkStudio\bin\RobWorkStudio.exe" "D:\10_Source_Repos\21_robot\RobWork\RobWork\GenericSixAxis.wc.xml"
```

Expected:

- RobWorkStudio opens without a startup error dialog.
- KinematicAnalysis plugin can be opened.
- A valid device is selectable in the top device combo.

## Scenario 1: Small Complete Run

Inputs:

- Tab: `Pose reachability`
- Source: `Manual rows`
- Manual position row: `x=0`, `y=0`, `z=0`
- Directions: `4`
- Rolls: `2`
- Collision: unchecked

Expected:

- Diagnostic label shows `Plan: 8 IK target(s), 8 orientation(s) per position`.
- Progress label starts at `Progress: 0 / 8 IK target(s)`.
- After completion, progress label shows `Progress: 8 / 8 IK target(s) (100.0%)`.
- Status line says `Pose reachability completed for 1 position(s).`
- Summary line shows `Positions: 1`, `Shown: 1`, and `Partial: 0`.
- Export CSV is enabled.

## Scenario 2: Cancel Run

Inputs:

- Source: `Manual rows`
- Add exactly two manual position rows: `0,0,0` and `0.1,0,0`.
- Directions: `100`
- Rolls: `10`
- Collision: unchecked

Action:

- Click `Run`.
- Click `Cancel` while the run is active.

Expected:

- Cancel button disables immediately after click.
- Status line first says `Pose reachability cancellation requested.`
- Final status starts with `Pose reachability canceled after ` and ends with ` position(s).`
- Progress completed count is less than planned count.
- Summary line shows `Partial: 1` when cancellation occurs mid-position.

## Scenario 3: Export Metadata

Action:

- Export Pose reachability CSV after Scenario 1 or Scenario 2.
- Export Report JSON from the Report tab.

Expected CSV:

- First line starts with `# pose_reachability_summary`.
- Summary line includes `partial`, `completed_ik_targets`, and `planned_ik_targets`.
- Header line ends with `partial,completed_ik_targets,planned_ik_targets`.

Expected JSON:

- Each `poseReachability` object contains `partial`, `completedIkTargets`, and `plannedIkTargets`.

## Scenario 4: Display Cap

Prepare exactly 501 task points:

```powershell
$csv = "D:\10_Source_Repos\21_robot\RobWork\RobWork\build\pose_reachability_501_taskpoints.csv"
"id,name,type,refFrame,tcpFrame,x,y,z,rollDeg,pitchDeg,yawDeg,positionToleranceMeters,orientationToleranceDeg,allowToolRollFree,weight,enabled,note" | Set-Content -LiteralPath $csv -Encoding ASCII
1..501 | ForEach-Object {
    $x = "{0:N6}" -f ($_ * 0.001)
    "P$_,Smoke $_,Generic,WORLD,TCP,$x,0,0,0,0,0,0.001,1,false,1,true,display cap smoke" | Add-Content -LiteralPath $csv -Encoding ASCII
}
```

Inputs:

- Import `build/pose_reachability_501_taskpoints.csv` in the Task points tab.
- Source: `Task points`
- Directions: `1`
- Rolls: `1`
- Collision: unchecked

Expected:

- Summary line shows `Positions: 501` and `Shown: 500`.
- Result table displays 500 rows.
- CSV export contains 501 pose reachability data rows, not only the 500 displayed rows.

## Result

- Scenario 1: NOT RUN
- Scenario 2: NOT RUN
- Scenario 3: NOT RUN
- Scenario 4: NOT RUN
- Notes: Run the scenarios manually and update the result above.
