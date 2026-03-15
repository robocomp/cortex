#ifndef DSR_GRAPH_GENERATOR_H
#define DSR_GRAPH_GENERATOR_H

#include <string>
#include <fstream>
#include <random>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <dsr/api/dsr_api.h>

namespace DSR::Benchmark {

// Graph topology types
enum class GraphTopology {
    Linear,      // Chain of nodes
    Star,        // Hub with spokes
    Tree,        // Hierarchical tree
    FullMesh,    // Every node connected to every other
    Random       // Random connections
};


// Configuration for synthetic graph generation
struct GraphGeneratorConfig {
    uint32_t num_nodes = 100;
    uint32_t edges_per_node = 2;
    GraphTopology topology = GraphTopology::Tree;
    std::string node_type = "test_node";
    std::string edge_type = "test_edge";
    bool include_rt_edges = false;
    bool include_attributes = true;
    uint32_t attributes_per_node = 3;
};


class GraphGenerator {
public:
    explicit GraphGenerator(unsigned int seed = std::random_device{}())
        : rng_(seed)
    {
        // Ensure test types are registered (safe to call multiple times)
        register_test_types();
    }

    // Register test node/edge types - call this before using any DSR operations
    static void register_test_types() {
        static bool registered = false;
        if (!registered) {
            node_types::register_type("test_node");
            edge_types::register_type("test_edge");
            registered = true;
        }
    }

    // Generate a config file with synthetic graph
    std::string generate_config_file(const GraphGeneratorConfig& config) {
        std::string filename = temp_filename();
        std::ofstream out(filename);
        if (!out.is_open()) {
            return "";
        }

        out << "{\n";
        out << "  \"DSRModel\": {\n";
        out << "    \"symbols\": {\n";

        // Generate root node
        out << generate_root_node();

        // Generate additional nodes based on topology
        auto node_ids = generate_node_ids(config.num_nodes);

        for (size_t i = 0; i < node_ids.size(); ++i) {
            out << ",\n";
            out << generate_node(node_ids[i], config, i);
        }

        out << "\n    }\n";
        out << "  }\n";
        out << "}\n";

        out.close();
        return filename;
    }

    // Generate small graph (100 nodes)
    std::string generate_small_graph() {
        GraphGeneratorConfig config;
        config.num_nodes = 100;
        config.topology = GraphTopology::Tree;
        return generate_config_file(config);
    }

    // Generate medium graph (1000 nodes)
    std::string generate_medium_graph() {
        GraphGeneratorConfig config;
        config.num_nodes = 1000;
        config.topology = GraphTopology::Tree;
        return generate_config_file(config);
    }

    // Generate large graph (10000 nodes)
    std::string generate_large_graph() {
        GraphGeneratorConfig config;
        config.num_nodes = 10000;
        config.topology = GraphTopology::Tree;
        config.include_attributes = false;  // Reduce size
        return generate_config_file(config);
    }

    // Generate empty config (just root)
    std::string generate_empty_graph() {
        std::string filename = temp_filename();
        std::ofstream out(filename);
        if (!out.is_open()) {
            return "";
        }

        out << "{\n";
        out << "  \"DSRModel\": {\n";
        out << "    \"symbols\": {\n";
        out << generate_root_node();
        out << "\n    }\n";
        out << "  }\n";
        out << "}\n";

        out.close();
        return filename;
    }

    // Add nodes directly to an existing graph
    void populate_graph(DSRGraph& graph, uint32_t num_nodes,
                        const std::string& node_type = "test_node") {
        uint64_t base_id = 1000;
        auto root = graph.get_node_root();
        uint64_t parent_id = root ? root->id() : 100;

        for (uint32_t i = 0; i < num_nodes; ++i) {
            DSR::Node node;
            node.id(base_id + i);
            node.name("bench_node_" + std::to_string(i));
            node.type(node_type);
            node.agent_id(graph.get_agent_id());

            // Add some attributes
            graph.add_attrib_local<level_att>(node, static_cast<int32_t>(i % 10));

            graph.insert_node(node);

            // Add edge from parent
            if (i > 0 && (i % 10) == 0) {
                parent_id = base_id + i - 1;
            }

            DSR::Edge edge;
            edge.from(parent_id);
            edge.to(node.id());
            edge.type("test_edge");
            edge.agent_id(graph.get_agent_id());
            graph.insert_or_assign_edge(edge);
        }
    }

    // Create a node for insertion benchmarks
    static DSR::Node create_test_node(uint64_t id, uint32_t agent_id,
                                       const std::string& name = "") {
        DSR::Node node;
        node.id(id);
        node.name(name.empty() ? "test_node_" + std::to_string(id) : name);
        node.type("test_node");
        node.agent_id(agent_id);
        return node;
    }

    // Create an edge for insertion benchmarks
    static DSR::Edge create_test_edge(uint64_t from, uint64_t to,
                                       uint32_t agent_id,
                                       const std::string& type = "test_edge") {
        DSR::Edge edge;
        edge.from(from);
        edge.to(to);
        edge.type(type);
        edge.agent_id(agent_id);
        return edge;
    }

private:
    std::string temp_filename() {
        std::string base = "/tmp/dsr_bench_";
        std::uniform_int_distribution<uint64_t> dist;
        return base + std::to_string(dist(rng_)) + ".json";
    }

    std::vector<uint64_t> generate_node_ids(uint32_t count) {
        std::vector<uint64_t> ids;
        ids.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            ids.push_back(1000 + i);  // Start from 1000 to avoid conflicts
        }
        return ids;
    }

    std::string generate_root_node() {
        return R"(      "100": {
        "attribute": {
          "level": {
            "type": 1,
            "value": 0
          }
        },
        "id": "100",
        "links": [],
        "name": "root",
        "type": "root"
      })";
    }

    std::string generate_node(uint64_t id, const GraphGeneratorConfig& config,
                              size_t index) {
        std::ostringstream oss;
        oss << "      \"" << id << "\": {\n";

        // Attributes
        oss << "        \"attribute\": {\n";
        oss << "          \"level\": {\n";
        oss << "            \"type\": 1,\n";
        oss << "            \"value\": " << (index % 10 + 1) << "\n";
        oss << "          }";

        if (config.include_attributes) {
            for (uint32_t a = 0; a < config.attributes_per_node; ++a) {
                oss << ",\n          \"attr_" << a << "\": {\n";
                oss << "            \"type\": 1,\n";
                oss << "            \"value\": " << (rng_() % 1000) << "\n";
                oss << "          }";
            }
        }

        oss << "\n        },\n";

        // ID and name
        oss << "        \"id\": \"" << id << "\",\n";

        // Links (edges)
        oss << "        \"links\": [";
        auto links = generate_links(id, config, index);
        for (size_t i = 0; i < links.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << "\n" << links[i];
        }
        if (!links.empty()) oss << "\n        ";
        oss << "],\n";

        // Name and type
        oss << "        \"name\": \"node_" << id << "\",\n";
        oss << "        \"type\": \"" << config.node_type << "\"\n";
        oss << "      }";

        return oss.str();
    }

    std::vector<std::string> generate_links(uint64_t from_id,
                                             const GraphGeneratorConfig& config,
                                             size_t index) {
        std::vector<std::string> links;

        // Always link back to root for tree topology
        if (config.topology == GraphTopology::Tree && index == 0) {
            links.push_back(generate_link(from_id, 100, config.edge_type, config.include_rt_edges));
        }

        // Generate additional links based on topology
        switch (config.topology) {
            case GraphTopology::Linear:
                if (index > 0) {
                    links.push_back(generate_link(from_id, 1000 + index - 1,
                                                   config.edge_type, config.include_rt_edges));
                } else {
                    links.push_back(generate_link(from_id, 100,
                                                   config.edge_type, config.include_rt_edges));
                }
                break;

            case GraphTopology::Star:
                links.push_back(generate_link(from_id, 100,
                                               config.edge_type, config.include_rt_edges));
                break;

            case GraphTopology::Tree: {
                // Each node links to its parent in tree
                uint64_t parent_id = (index == 0) ? 100 : (1000 + (index - 1) / 2);
                links.push_back(generate_link(from_id, parent_id,
                                               config.edge_type, config.include_rt_edges));
                break;
            }

            case GraphTopology::FullMesh:
                // Limited to avoid explosion
                for (uint64_t target = 1000; target < from_id && links.size() < 5; ++target) {
                    links.push_back(generate_link(from_id, target,
                                                   config.edge_type, config.include_rt_edges));
                }
                break;

            case GraphTopology::Random: {
                std::uniform_int_distribution<uint32_t> count_dist(1, config.edges_per_node);
                std::uniform_int_distribution<uint64_t> id_dist(100, 1000 + index - 1);
                uint32_t num_links = (index == 0) ? 1 : count_dist(rng_);
                for (uint32_t i = 0; i < num_links; ++i) {
                    uint64_t target = (index == 0) ? 100 : id_dist(rng_);
                    links.push_back(generate_link(from_id, target,
                                                   config.edge_type, config.include_rt_edges));
                }
                break;
            }
        }

        return links;
    }

    std::string generate_link(uint64_t from, uint64_t to,
                              const std::string& type, bool include_rt) {
        std::ostringstream oss;
        oss << "          {\n";
        oss << "            \"dst\": \"" << to << "\",\n";
        oss << "            \"label\": \"" << type << "\",\n";
        oss << "            \"linkAttribute\": {";

        if (include_rt && type == "RT") {
            oss << R"(
              "rt_rotation_euler_xyz": {
                "type": 3,
                "value": [0, 0, 0]
              },
              "rt_translation": {
                "type": 3,
                "value": [0, 0, 0]
              })";
        }

        oss << "},\n";
        oss << "            \"src\": \"" << from << "\"\n";
        oss << "          }";

        return oss.str();
    }

    std::mt19937 rng_;
};

}  // namespace DSR::Benchmark

#endif  // DSR_GRAPH_GENERATOR_H
