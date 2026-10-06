# C4D_MakePolygon

**C4D_MakePolygon** is a high-performance C++ plugin for **Maxon Cinema 4D** replicating the iconic Foundry Modo **`P`** hotkey tool (**Make Polygon / Fill Hole**).

It provides an instant, context-aware command that creates triangles, quads, or N-gons and fills holes directly from selected vertices or edges with a single keystroke.

---

## Features

### 1. Point / Vertex Mode (`Mpoints`)
* **3 Vertices Selected:** Instantly generates a clean triangle (`CPolygon`).
* **4 Vertices Selected:** Builds a quadrangle (`CPolygon`) with automatic cyclic ordering around the planar centroid. This prevents self-intersecting quads ("bowties") regardless of selection order.
* **5+ Vertices Selected:** Creates a proper N-Gon using the Maxon Modeling Engine, preserving UVW maps, vertex colors, and selection tags.
* **Manifold Normal Alignment:** Automatically resolves surface winding order (right-hand rule) to match adjacent geometry. If isolated in space, the normal aligns towards the active viewport camera.

### 2. Edge Mode (`Medges`) & Hole Capping
* **1 Boundary Edge Selected (Fill Hole):** Automatically traces the entire connected boundary loop (Boundary Loop Walker) and caps the open hole with a polygon.
* **2 Edges Selected (Modo 14+ Make Quad):**
  * **Corner Edges (Sharing a vertex):** Always builds a quad! Snaps to an existing 4th boundary vertex if present; if not, calculates and creates a new 4th corner vertex extrapolated along the parallelogram vector.
  * **Parallel / Disjoint Boundary Edges:** Bridges the two edges with a quad, automatically choosing the diagonal pairing with minimal distortion.
* **3+ Edges Selected:** Builds a face directly along the closed cycle of selected edges.

### 3. Native Cinema 4D Integration
* **Undo / Redo Support:** Fully integrated with `doc->StartUndo()` and `doc->AddUndo(UNDOTYPE::CHANGE)`.
* **Auto-Selection:** Newly created polygons are automatically selected and highlighted.
* **Multi-Version Compatibility:** Built and validated for Cinema 4D **2025** and **2026** (Windows x64).

---

## Assigning the `P` Hotkey in Cinema 4D

To match the Modo workflow:

1. In Cinema 4D, open **Window → Customization → Customize Commands...** (or press `Shift + F12`).
2. In the search filter, type: `Make Polygon (Modo P)`.
3. Select the command and click in the **Shortcut** input field.
4. Press **`P`** (or your preferred shortcut).
5. Click **Assign**.

---

## Installation

1. Download the latest release package: [`C4D_MakePolygon_v1.0.0.zip`](C4D_MakePolygon_v1.0.0.zip).
2. Extract the folder corresponding to your Cinema 4D version (`2025` or `2026`) into your Cinema 4D `plugins` directory:
   * **Windows:** `C:\Program Files\Maxon Cinema 4D 2026\plugins\C4D_MakePolygon`
   * *Or your custom plugins directory configured in Preferences → Plugins.*
3. Restart Cinema 4D.

---

## Building from Source

### Prerequisites
* **Visual Studio 2022** (v143 toolset) with C++ Desktop Development tools
* **CMake 3.24+**
* **Cinema 4D SDK 2025 or 2026**

### Build Scripts
The repository includes automated PowerShell and batch build scripts:

* `build_2026.bat` / `build_2026.ps1` — Configures CMake and compiles Release x64 for Cinema 4D 2026.
* `build_2025.bat` / `build_2025.ps1` — Configures CMake and compiles Release x64 for Cinema 4D 2025.
* `pack_release.bat` — Packages both 2025 and 2026 binaries into a distributable `.zip` archive.
* `deploy_2026.bat` / `deploy_2026.ps1` — Deploys built binaries to a designated test/plugins folder.

---

## License

MIT License. See [LICENSE](LICENSE) for details.
