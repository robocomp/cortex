# CORTEX API Guide

This document summarizes the most commonly used public APIs in CORTEX and shows how to use them from both C++ and Python.

Covered APIs:

- `DSRGraph` from `dsr_api`
- `RT_API`
- `InnerEigenAPI`
- `CameraAPI`

Notes:

- C++ and Python do not expose exactly the same surface.
- `InnerEigenAPI` uses different argument ordering in C++ and Python:
  - C++: `dest, orig, ...`
  - Python: `orig, dest, ...`
- `CameraAPI` exists in C++, but its Python binding block is currently commented out in `python-wrapper/python_api.cpp`. The Python example in this guide is marked as intended usage, not currently available behavior.

## 1. Common Setup

### C++

```cpp
#include <dsr/api/dsr_api.h>

int main()
{
    DSR::DSRGraph graph("demo_graph", 7, "scene.json");

    auto rt = graph.get_rt_api();
    auto inner = graph.get_inner_eigen_api();

    return 0;
}
```

### Python

```python
import pydsr

graph = pydsr.DSRGraph(0, "demo_graph", 7, "scene.json")

rt = pydsr.rt_api(graph)
inner = pydsr.inner_api(graph)
```

`DSRGraph` in Python still receives the deprecated `root` constructor argument. In the current implementation it is ignored, so using `0` is fine.

## 2. `DSRGraph` (`dsr_api`)

`DSRGraph` is the main entry point. It stores nodes, edges, and attributes, and it is the factory for the RT, inner-eigen, and camera sub-APIs.

### Main C++ Methods

```cpp
std::optional<Node> get_node(const std::string &name);
std::optional<Node> get_node(uint64_t id);

template<typename No>
std::optional<uint64_t> insert_node(No &&node);

template<typename No>
bool update_node(No &&node);

bool delete_node(const DSR::Node& node);
bool delete_node(const std::string &name);
bool delete_node(uint64_t id);

std::optional<Edge> get_edge(const std::string& from, const std::string& to, const std::string& key);
std::optional<Edge> get_edge(uint64_t from, uint64_t to, const std::string& key);
bool insert_or_assign_edge(DSR::Edge &edge);
bool delete_edge(uint64_t from, uint64_t to, const std::string& key);

std::optional<Node> get_node_root();
std::vector<Node> get_nodes_by_type(const std::string &type);
std::vector<Node> get_nodes();
std::optional<std::string> get_name_from_id(uint64_t id);
std::optional<uint64_t> get_id_from_name(const std::string &name);
std::optional<std::map<std::pair<uint64_t, std::string>, Edge>> get_edges(uint64_t id);
std::vector<Edge> get_edges_by_type(const std::string &type);
std::vector<Edge> get_edges_to_id(uint64_t id);

std::unique_ptr<InnerEigenAPI> get_inner_eigen_api();
std::unique_ptr<RT_API> get_rt_api();
std::unique_ptr<CameraAPI> get_camera_api(const DSR::Node &camera_node);
```

### Main Python Methods

```python
graph.get_agent_id()
graph.get_agent_name()

graph.get_node(id)
graph.get_node(name)
graph.insert_node(node)
graph.update_node(node)
graph.delete_node(id)
graph.delete_node(name)

graph.get_edge(from_name, to_name, edge_type)
graph.get_edge(from_id, to_id, edge_type)
graph.insert_or_assign_edge(edge)
graph.delete_edge(from_id, to_id, edge_type)
graph.delete_edge(from_name, to_name, edge_type)

graph.get_node_root()
graph.get_nodes_by_type(type_name)
graph.get_nodes()
graph.get_name_from_id(node_id)
graph.get_id_from_name(name)
graph.get_edges(node_id)
graph.get_edges_by_type(edge_type)
graph.get_edges_to_id(node_id)
graph.write_to_json_file(path, skip_atts=[])
```

### C++ Example: Node and Edge CRUD

```cpp
#include <dsr/api/dsr_api.h>

void graph_example()
{
    DSR::DSRGraph graph("demo_graph", 7);

    auto room = DSR::Node::create<room_node_type>("room_1");
    graph.add_or_modify_attrib_local<level_att>(room, 1);
    graph.add_or_modify_attrib_local<pos_x_att>(room, 0.f);
    graph.add_or_modify_attrib_local<pos_y_att>(room, 0.f);

    auto room_id = graph.insert_node(room);
    if (not room_id.has_value())
        return;

    auto robot = DSR::Node::create<omnirobot_node_type>("robot_1");
    graph.add_or_modify_attrib_local<level_att>(robot, 2);
    auto robot_id = graph.insert_node(robot);
    if (not robot_id.has_value())
        return;

    auto edge = DSR::Edge::create<has_edge_type>(*room_id, *robot_id);
    graph.insert_or_assign_edge(edge);

    if (auto fetched = graph.get_node("robot_1"); fetched.has_value())
    {
        graph.add_or_modify_attrib_local<pos_x_att>(*fetched, 150.f);
        graph.update_node(*fetched);
    }
}
```

### Python Example: Node and Edge CRUD

```python
import pydsr

graph = pydsr.DSRGraph(0, "demo_graph", 7)

room = pydsr.Node(7, "room", "room_1")
room.attrs["level"] = pydsr.Attribute(1)
room.attrs["pos_x"] = pydsr.Attribute(0.0)
room.attrs["pos_y"] = pydsr.Attribute(0.0)
room_id = graph.insert_node(room)

robot = pydsr.Node(7, "omnirobot", "robot_1")
robot.attrs["level"] = pydsr.Attribute(2)
robot_id = graph.insert_node(robot)

edge = pydsr.Edge(robot_id, room_id, "has", 7)
graph.insert_or_assign_edge(edge)

robot_node = graph.get_node("robot_1")
if robot_node is not None:
    robot_node.attrs["pos_x"] = pydsr.Attribute(150.0)
    graph.update_node(robot_node)
```

### C++ Attribute Helpers

The C++ API also provides typed attribute helpers that do not exist in the same form in Python:

```cpp
auto room = graph.get_node("room_1");
if (room.has_value())
{
    auto px = graph.get_attrib_by_name<pos_x_att>(*room);
    auto py = graph.get_attrib_by_name<pos_y_att>(*room);

    graph.modify_attrib_local<pos_x_att>(*room, 300.f);
    graph.modify_attrib_local<pos_y_att>(*room, 120.f);
    graph.update_node(*room);
}
```

## 3. `RT_API`

`RT_API` stores SE(3) transforms on RT edges and supports both nearest-sample lookup and interpolated lookup over the edge history.

### Time Query Modes

```cpp
enum class TimeQuery
{
    Nearest,
    Interpolated
};
```

Python enum:

```python
pydsr.rt_api.time_query.nearest
pydsr.rt_api.time_query.interpolated
```

### Main C++ Methods

```cpp
void insert_or_assign_edge_RT(Node &n, uint64_t to,
                              const std::vector<float> &trans,
                              const std::vector<float> &rot_euler,
                              std::optional<uint64_t> timestamp = std::nullopt);

static std::optional<Edge> get_edge_RT(const Node &n, uint64_t to,
                                       const std::string &edge_type = "RT");

std::optional<Mat::RTMat> get_RT_pose_from_parent(const Node &n,
                                                  const std::string &edge_type = "RT");

std::optional<Mat::RTMat> get_edge_RT_as_rtmat(const Edge &edge,
                                               std::uint64_t timestamp = 0,
                                               TimeQuery time_query = TimeQuery::Nearest);

std::optional<Eigen::Vector3d> get_translation(const Node &n,
                                               uint64_t to,
                                               std::uint64_t timestamp = 0,
                                               TimeQuery time_query = TimeQuery::Nearest);

std::optional<Eigen::Vector3d> get_translation(uint64_t node_id,
                                               uint64_t to,
                                               std::uint64_t timestamp = 0,
                                               TimeQuery time_query = TimeQuery::Nearest);
```

### Main Python Methods

```python
rt.insert_or_assign_edge_RT(node, to, trans, rot_euler, timestamp=None)
rt_api.get_edge_RT(node, to, type_edge="RT")
rt.get_RT_pose_from_parent(node, type_edge="RT")
rt.get_edge_RT_as_rtmat(edge, timestamp=0, time_query=pydsr.rt_api.time_query.nearest)
rt.get_translation(node_id, to, timestamp=0, time_query=pydsr.rt_api.time_query.nearest)
```

### C++ Example: Add History and Query It

```cpp
auto rt = graph.get_rt_api();

auto root = graph.get_node("root");
auto room = graph.get_node("room_1");
if (not root.has_value() || not room.has_value())
    return;

rt->insert_or_assign_edge_RT(*root, room->id(), {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1000);
rt->insert_or_assign_edge_RT(*root, room->id(), {10.f, 0.f, 0.f}, {0.f, 0.f, 1.5707963f}, 2000);

auto edge = graph.get_edge(root->id(), room->id(), "RT");
if (edge.has_value())
{
    auto nearest_tf = rt->get_edge_RT_as_rtmat(*edge, 1500);
    auto interp_tf = rt->get_edge_RT_as_rtmat(*edge, 1500, DSR::RT_API::TimeQuery::Interpolated);
}

auto nearest_tr = rt->get_translation(root->id(), room->id(), 1500);
auto interp_tr = rt->get_translation(root->id(), room->id(), 1500,
                                     DSR::RT_API::TimeQuery::Interpolated);
```

### Python Example: Nearest vs Interpolated Query

```python
import math
import pydsr

graph = pydsr.DSRGraph(0, "demo_graph", 7)
rt = pydsr.rt_api(graph)

root = graph.get_node("root")
room = graph.get_node("room_1")

rt.insert_or_assign_edge_RT(root, room.id, [0.0, 0.0, 0.0], [0.0, 0.0, 0.0], 1000)
rt.insert_or_assign_edge_RT(root, room.id, [10.0, 0.0, 0.0], [0.0, 0.0, math.pi / 2.0], 2000)

edge = pydsr.rt_api.get_edge_RT(root, room.id)
nearest_tf = rt.get_edge_RT_as_rtmat(edge, 1500, pydsr.rt_api.time_query.nearest)
interp_tf = rt.get_edge_RT_as_rtmat(edge, 1500, pydsr.rt_api.time_query.interpolated)

nearest_tr = rt.get_translation(root.id, room.id, 1500, pydsr.rt_api.time_query.nearest)
interp_tr = rt.get_translation(root.id, room.id, 1500, pydsr.rt_api.time_query.interpolated)
```

## 4. `InnerEigenAPI`

`InnerEigenAPI` composes RT edges through the kinematic tree and returns frame-to-frame transforms, translations, rotations, and Euler angles.

Important detail:

- C++ method order is `dest, orig, ...`
- Python method order is `orig, dest, ...`

### Main C++ Methods

```cpp
std::optional<Mat::Vector3d> transform(const std::string &dest,
                                       const std::string &orig,
                                       std::uint64_t timestamp = 0,
                                       const std::string &edge_type = "RT",
                                       RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);

std::optional<Mat::Vector3d> transform(const std::string &dest,
                                       const Mat::Vector3d &vector,
                                       const std::string &orig,
                                       std::uint64_t timestamp = 0,
                                       const std::string &edge_type = "RT",
                                       RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);

std::optional<Mat::Vector6d> transform_axis(...);

std::optional<Mat::RTMat> get_transformation_matrix(const std::string &dest,
                                                    const std::string &orig,
                                                    std::uint64_t timestamp = 0,
                                                    const std::string &edge_type = "RT",
                                                    RT_API::TimeQuery time_query = RT_API::TimeQuery::Nearest);

std::optional<Mat::Rot3D> get_rotation_matrix(...);
std::optional<Mat::Vector3d> get_translation_vector(...);
std::optional<Mat::Vector3d> get_euler_xyz_angles(...);
```

### Main Python Methods

```python
inner.transform(orig, dest, timestamp=0, type_edge="RT",
                time_query=pydsr.rt_api.time_query.nearest)
inner.transform(orig, vector, dest, timestamp=0, type_edge="RT",
                time_query=pydsr.rt_api.time_query.nearest)

inner.transform_axis(orig, dest, timestamp=0, type_edge="RT",
                     time_query=pydsr.rt_api.time_query.nearest)

inner.get_transformation_matrix(orig, dest, timestamp=0, type_edge="RT",
                                time_query=pydsr.rt_api.time_query.nearest)
inner.get_rotation_matrix(orig, dest, timestamp=0, type_edge="RT",
                          time_query=pydsr.rt_api.time_query.nearest)
inner.get_translation_vector(orig, dest, timestamp=0, type_edge="RT",
                             time_query=pydsr.rt_api.time_query.nearest)
inner.get_euler_xyz_angles(orig, dest, timestamp=0, type_edge="RT",
                           time_query=pydsr.rt_api.time_query.nearest)
```

### C++ Example: Chain Transform with Interpolation

```cpp
auto inner = graph.get_inner_eigen_api();

auto nearest = inner->get_translation_vector(
    "robot_1", "root", 1500, "RT", DSR::RT_API::TimeQuery::Nearest);

auto interpolated = inner->get_translation_vector(
    "robot_1", "root", 1500, "RT", DSR::RT_API::TimeQuery::Interpolated);

auto tf = inner->get_transformation_matrix(
    "robot_1", "root", 1500, "RT", DSR::RT_API::TimeQuery::Interpolated);

auto point = inner->transform(
    "robot_1", Mat::Vector3d(1.0, 0.0, 0.0), "root", 1500,
    "RT", DSR::RT_API::TimeQuery::Interpolated);
```

### Python Example: Chain Transform with Interpolation

```python
import pydsr

graph = pydsr.DSRGraph(0, "demo_graph", 7)
inner = pydsr.inner_api(graph)

nearest = inner.get_translation_vector(
    "root", "Shadow", 1500, "RT", pydsr.rt_api.time_query.nearest)

interpolated = inner.get_translation_vector(
    "root", "Shadow", 1500, "RT", pydsr.rt_api.time_query.interpolated)

tf = inner.get_transformation_matrix(
    "root", "Shadow", 1500, "RT", pydsr.rt_api.time_query.interpolated)

point = inner.transform(
    "root", [1.0, 0.0, 0.0], "Shadow", 1500,
    "RT", pydsr.rt_api.time_query.interpolated)
```

Remember that in Python the first string argument is `orig` and the second one is `dest`.

## 5. `CameraAPI`

`CameraAPI` wraps camera geometry and image/depth access for a camera node already present in the graph.

### Main C++ Methods

```cpp
std::optional<std::vector<uint8_t>> get_rgb_image();
std::optional<std::vector<float>> get_depth_image();
std::optional<std::vector<std::tuple<float,float,float>>> get_pointcloud(
    const std::string& target_frame_node = "", unsigned short subsampling = 1);
std::optional<std::vector<uint8_t>> get_depth_as_gray_image() const;

std::optional<std::tuple<float,float,float>> get_roi_depth(
    const std::vector<float> &depth,
    const Eigen::AlignedBox<float, 2> &roi);

bool reload_camera(const DSR::Node &n);
std::uint64_t get_id() const;
float get_focal() const;
float get_focal_x() const;
float get_focal_y() const;
std::uint32_t get_width() const;
std::uint32_t get_height() const;
int get_depth() const;
Eigen::Vector2d project(const Eigen::Vector3d &p, int cx = -1, int cy = -1) const;
Eigen::Vector3d get_ray(const Eigen::Vector3d &p) const;
Eigen::Vector3d get_angles(const Eigen::Vector3d &p) const;
```

### C++ Example: Create and Use a Camera API

```cpp
auto camera_node = graph.get_node("camera_head");
if (not camera_node.has_value())
    return;

auto camera = graph.get_camera_api(*camera_node);

auto rgb = camera->get_rgb_image();
auto depth = camera->get_depth_image();
auto cloud = camera->get_pointcloud("root", 2);

Eigen::Vector2d pixel = camera->project(Eigen::Vector3d(0.5, 0.0, 1.5));
Eigen::Vector3d ray = camera->get_ray(Eigen::Vector3d(pixel.x(), pixel.y(), 1.0));
```

### Python Status

The `camera_api` pybind block is currently commented out in `python-wrapper/python_api.cpp`, so it is not available from `pydsr` in the current build.

The intended Python usage, once that binding is enabled, would look like this:

```python
camera_node = graph.get_node("camera_head")
camera = pydsr.camera_api(graph, camera_node)

rgb = camera.get_rgb_image()
depth = camera.get_depth_image()
cloud = camera.get_pointcloud("root", 2)
pixel = camera.project([0.5, 0.0, 1.5])
```

## 6. Recommended Usage Patterns

### Use typed factories and typed attributes in C++

```cpp
auto robot = DSR::Node::create<robot_node_type>("robot_1");
graph.add_or_modify_attrib_local<name_att>(robot, std::string("robot_1"));
graph.add_or_modify_attrib_local<level_att>(robot, 1);
graph.insert_node(robot);
```

### Use `Nearest` as the compatibility default

If you omit the time-query mode, the behavior remains the previous one:

```cpp
auto tr = rt->get_translation(root_id, room_id, 1500);  // nearest sample
```

Use `Interpolated` only when you explicitly want interpolation:

```cpp
auto tr = rt->get_translation(root_id, room_id, 1500,
                              DSR::RT_API::TimeQuery::Interpolated);
```

### Prefer `RT_API` for single-edge temporal queries

If you want the history of one RT edge, use `RT_API` directly.

Use `InnerEigenAPI` when you need transforms between arbitrary nodes in the kinematic tree.

## 7. Current Limitations

- `CameraAPI` is not currently exported in Python because the pybind block is commented out.
- The C++ and Python argument order differs in `InnerEigenAPI`.
- `DSRGraph` exposes many template-heavy typed attribute helpers in C++; those do not map one-to-one to Python.

## 8. File Reference

The APIs documented here are defined in:

- `api/include/dsr/api/dsr_api.h`
- `api/include/dsr/api/dsr_rt_api.h`
- `api/include/dsr/api/dsr_inner_eigen_api.h`
- `api/include/dsr/api/dsr_camera_api.h`
- `python-wrapper/python_api.cpp`