---
type: community
cohesion: 0.25
members: 9
---

# Radiometry and Dosimetry

**Cohesion:** 0.25 - loosely connected
**Members:** 9 nodes

## Members
- [[Dosimeter ADC Acquisition]] - concept - Knowledge Base/Interfaces/Dosimeter Telemetry.md
- [[Dosimeter Telemetry]] - document - Knowledge Base/Interfaces/Dosimeter Telemetry.md
- [[Frame Dosimeter Metadata]] - concept - Knowledge Base/Interfaces/Dosimeter Telemetry.md
- [[P-003 Proposed Raw Video Mode]] - concept - Knowledge Base/Project/Decision Log.md
- [[Phase 4 UVC and Dosimeter]] - concept - Knowledge Base/Project/TODO.md
- [[Thermal Frame Representation]] - concept - Knowledge Base/Interfaces/Lepton Interface.md
- [[Two-Point Dosimeter Calibration Model]] - concept - Knowledge Base/Interfaces/Dosimeter Telemetry.md
- [[UVC Radiometric Video]] - concept - Knowledge Base/Interfaces/USB Interface.md
- [[Uncalibrated Measurement Policy]] - rationale - Knowledge Base/Interfaces/Dosimeter Telemetry.md

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Radiometry_and_Dosimetry
SORT file.name ASC
```

## Connections to other communities
- 1 edge to [[_COMMUNITY_Project Governance and Hardware Review]]
- 1 edge to [[_COMMUNITY_Lepton Control and Capture]]
- 1 edge to [[_COMMUNITY_USB Architecture and Blockers]]

## Top bridge nodes
- [[UVC Radiometric Video]] - degree 4, connects to 1 community
- [[Dosimeter Telemetry]] - degree 3, connects to 1 community
- [[Thermal Frame Representation]] - degree 3, connects to 1 community