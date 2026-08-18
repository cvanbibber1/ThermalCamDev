---
type: community
cohesion: 0.21
members: 12
---

# Runtime Services and Data Flow

**Cohesion:** 0.21 - loosely connected
**Members:** 12 nodes

## Members
- [[D-003 One Shared Command Model]] - rationale - Knowledge Base/Project/Decision Log.md
- [[D-006 Capture Never Blocks on Transport]] - rationale - Knowledge Base/Project/Decision Log.md
- [[Dosimeter Service]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Field-Bus Service]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Frame Acquisition Service]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Frame Broker]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Health Service]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Nonblocking Capture Pipeline]] - rationale - Knowledge Base/Architecture/System Specification v0.1.md
- [[Runtime Service Architecture]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Shared Command Model]] - concept - Knowledge Base/Architecture/System Specification v0.1.md
- [[Static Buffer Ownership]] - rationale - Knowledge Base/Architecture/System Specification v0.1.md
- [[USB Service]] - concept - Knowledge Base/Architecture/System Specification v0.1.md

## Live Query (requires Dataview plugin)

```dataview
TABLE source_file, type FROM #community/Runtime_Services_and_Data_Flow
SORT file.name ASC
```

## Connections to other communities
- 2 edges to [[_COMMUNITY_Lepton Control and Capture]]
- 1 edge to [[_COMMUNITY_Project Governance and Hardware Review]]
- 1 edge to [[_COMMUNITY_Shared Transport Framing]]

## Top bridge nodes
- [[Runtime Service Architecture]] - degree 8, connects to 2 communities
- [[Shared Command Model]] - degree 4, connects to 1 community
- [[Frame Acquisition Service]] - degree 3, connects to 1 community