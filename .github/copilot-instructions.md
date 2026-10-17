# Copilot Instructions — CORTEX / DSR

## What this project is

CORTEX is a robotics cognitive architecture where multiple autonomous agents share a **Distributed Scene Representation (DSR) graph** as working memory. The graph is replicated across all agents using **FastDDS (eProsima)** in reliable multicast mode and kept eventually consistent via **delta-based CRDTs**. Agents are C++ (or Python) programs that read/write nodes, edges, and attributes on their local graph copy; changes propagate automatically to all other agents.

Four libraries are built:
- **`dsr_core`** — CRDT types, FastDDS publisher/subscriber, type-checking machinery
- **`dsr_api`** — High-level `DSRGraph` class + sub-APIs (RT, InnerEigen, Camera, AgentInfo)
- **`dsr_gui`** — Qt visualization widgets (graph view, 2D scene views, tree view)
- **`pydsr`** — pybind11 Python bindings for the full API

## Build

**Platform**: Ubuntu 20.04 or 22.04 only.

```bash
# Install dependencies (one-time)
sudo apt install libasio-dev libtinyxml2-dev libopencv-dev libeigen3-dev \
  python3-dev python3-pybind11 cmake gcc-11 g++-11

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)
sudo make install && sudo ldconfig
```

**Useful CMake flags:**
| Flag | Effect |
|------|--------|
| `-DWITH_TESTS=ON` | Build the Catch2 test binary (`tests/tests`) |
| `-DSANITIZER=ON` | Enable AddressSanitizer + UBSan (Debug only) |
| `-DPYBIND_FORCE_PYTHON310=ON` | Force Python 3.10 for pybind11 |

Compile flags applied to all targets: `-std=c++23 -O3 -g -fmax-errors=5`

## Tests

Tests use **Catch2 v3.8.0** (fetched automatically via CMake `FetchContent`). Build with `-DWITH_TESTS=ON`, then from the `tests/` build output:

```bash
./tests                        # run all tests
./tests "[NODE]"               # run tests tagged [NODE]
./tests "[CRDT]"               # run CRDT tests
./tests "[SYNC]"               # run synchronization tests
./tests --list-tests           # list all test names
./tests -v                     # verbose output
./tests -r junit               # JUnit XML reporter
```

Test source files are in `tests/` and cover:
- `graph/` — node/edge CRUD, attributes, convenience ops, RT timestamps
- `crdt/` — CRDT delta operations
- `synchronization/` — multi-agent consistency, type translation, Qt signals

## Key conventions

### Type-safe node/edge/attribute creation

**Never** use the raw constructors directly (they are `[[deprecated]]`). Use the factory templates:

```cpp
// Nodes — type is validated at compile time
auto node = DSR::Node::create<robot_node_type>("my_robot");
g->insert_node(node);

// Edges — type is validated at compile time
auto edge = DSR::Edge::create<rt_edge_type>(from_id, to_id);
g->insert_or_assign_edge(edge);

// Attributes — name and type are validated at compile time
g->add_or_modify_attrib_local<pos_x_att>(node, 1.5f);
auto val = g->get_attrib_by_name<pos_x_att>(node); // returns std::optional<float>
```

Node types (`robot_node_type`, `transform_node_type`, …) are declared with `REGISTER_NODE_TYPE` in `core/include/dsr/core/types/type_checking/dsr_node_type.h`. Attribute names (`pos_x_att`, `name_att`, …) are declared with `REGISTER_ATTR_TYPE` in `dsr_attr_name.h`. Adding new types means adding a macro call there.

### `std::optional<T>` everywhere — no exceptions for graph misses

All graph lookups return `std::optional`. Always check before use:

```cpp
if (auto node = g->get_node("robot"); node.has_value()) { ... }
if (auto val = g->get_attrib_by_name<pos_x_att>(*node); val.has_value()) { ... }
```

### Move semantics on insert/update

`insert_node`, `update_node`, and `insert_or_assign_edge` accept forwarding references (`No&&`). Pass by move when you no longer need the local copy:

```cpp
g->insert_node(std::move(node));
```

### Sub-APIs are lazily initialised

Do not construct sub-API objects manually. Obtain them from the graph:

```cpp
auto rt  = g->get_rt_api();            // RT_API*
auto inn = g->get_inner_eigen_api();   // InnerEigenAPI*
auto cam = g->get_camera_api();        // CameraAPI*
```

### Qt signals for graph events

`DSRGraph` inherits `QObject` and emits typed signals. Connect them in agent constructors:

```cpp
QObject::connect(g.get(), &DSR::DSRGraph::update_node_signal,
                 this, &MyAgent::node_updated);
```

Available signals: `update_node_signal`, `update_edge_signal`, `del_node_signal`, `del_edge_signal`, and attribute-specific variants.

### Member naming

- Class members: `m_` prefix (`m_type`, `m_attrs`, `m_agent_id`)
- Free functions / methods: `snake_case`
- Types / classes: `PascalCase` (`DSRGraph`, `Node`, `Edge`)
- Constants: `UPPER_SNAKE_CASE` (`TIMEOUT`, `BLOCK_SIZE`)
- Source files: `dsr_*.h` / `dsr_*.cpp`

### The `root` node and RT tree

A special node named `"root"` is always present and acts as the origin of the kinematic tree. Geometric relationships between nodes use **RT edges** (`rt_edge_type`) that carry `rotation_euler_xyz` and `translation` attributes. Use `RT_API` to read/write these; use `InnerEigenAPI` for computed transforms between arbitrary nodes.

## Python API (`pydsr`)

```python
import pydsr

g = pydsr.DSRGraph(0, "agent_name", agent_id)

node = g.get_node("robot")          # returns Node or None
g.insert_node(node)
g.update_node(node)
g.delete_node(node_id)

nodes = g.get_nodes_by_type("robot")
edges = g.get_edges_by_type("rt")

rt  = pydsr.rt_api(g)
inn = pydsr.inner_api(g)

# Signals
pydsr.signals.connect(g, pydsr.signals.signal_type.UPDATE_NODE, callback)
```

## Project structure (key paths)

```
api/include/dsr/api/dsr_api.h          # DSRGraph — main entry point
api/include/dsr/api/dsr_rt_api.h       # RT_API — kinematic transforms
api/include/dsr/api/dsr_inner_eigen_api.h  # InnerEigenAPI — Eigen transforms
core/include/dsr/core/types/user_types.h   # Node, Edge, Attribute definitions
core/include/dsr/core/types/type_checking/ # Compile-time type/attr registries
core/include/dsr/core/crdt/delta_crdt.h   # CRDT delta implementation
core/rtps/                             # FastDDS publisher/subscriber
python-wrapper/python_api.cpp          # pybind11 bindings
tests/                                 # Catch2 test suite
```
