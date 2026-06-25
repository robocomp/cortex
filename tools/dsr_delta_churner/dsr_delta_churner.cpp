#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <dsr/api/dsr_api.h>
#include <dsr/core/types/type_checking/dsr_edge_type.h>
#include <dsr/core/types/type_checking/dsr_node_type.h>

namespace
{
int arg_int(int argc, char **argv, const std::string &name, int fallback)
{
    const std::string prefix = name + "=";
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0)
            return std::stoi(arg.substr(prefix.size()));
    }
    return fallback;
}

std::string arg_string(int argc, char **argv, const std::string &name, const std::string &fallback)
{
    const std::string prefix = name + "=";
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0)
            return arg.substr(prefix.size());
    }
    return fallback;
}
}

int main(int argc, char **argv)
{
    const int agent_id = arg_int(argc, argv, "--agent-id", 1000);
    const int count = arg_int(argc, argv, "--count", 20);
    const int hold_ms = arg_int(argc, argv, "--hold-ms", 300);
    const int settle_ms = arg_int(argc, argv, "--settle-ms", 500);
    const std::string run_tag = arg_string(argc, argv, "--tag", std::to_string(::getpid()));

    DSR::GraphSettings settings{
        static_cast<uint32_t>(agent_id),
        4,
        1,
        "delta_churner_" + run_tag,
        "",
        "",
        true,
        DSR::GraphSettings::LOGLEVEL::INFOL,
        0,
        DSR::SignalMode::Queue,
        DSR::SyncMode::LWW,
    };
    DSR::DSRGraph graph(settings);

    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

    std::vector<uint64_t> inserted;
    inserted.reserve(count);
    const auto root = graph.get_node_root();
    const auto root_id = root.has_value() ? root->id() : uint64_t{100};

    for (int i = 0; i < count; ++i)
    {
        auto node = DSR::Node::create<object_node_type>("delta_churn_" + run_tag + "_" + std::to_string(i));
        node.agent_id(graph.get_agent_id());
        auto id = graph.insert_node(node);
        if (!id.has_value())
        {
            std::cerr << "insert_node failed for " << i << '\n';
            continue;
        }

        inserted.push_back(*id);
        DSR::Edge edge;
        edge.from(root_id);
        edge.to(*id);
        edge.type("RT");
        edge.agent_id(graph.get_agent_id());
        if (!graph.insert_or_assign_edge(edge))
            std::cerr << "insert_or_assign_edge failed for node " << *id << '\n';
    }

    std::cout << "created " << inserted.size() << " nodes, holding for " << hold_ms << " ms\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));

    for (auto it = inserted.rbegin(); it != inserted.rend(); ++it)
    {
        if (!graph.delete_node(*it))
            std::cerr << "delete_node failed for " << *it << '\n';
    }

    std::cout << "deleted " << inserted.size() << " nodes\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
    return EXIT_SUCCESS;
}
