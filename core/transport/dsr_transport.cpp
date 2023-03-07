//
// Created by jc on 19/09/22.
//

#include "dsr/core/transport/dsr_transport.h"

#include "dsr/core/types/crdt_types.h"

#include <functional>
#include <future>
#include <memory>
#include <utility>

using namespace std::literals;
using namespace DSR;

Transport::Transport(std::unique_ptr<BaseManager> comm_) : comm(std::move(comm_)){};

auto Transport::create(std::unique_ptr<BaseManager> comm_) -> std::shared_ptr<Transport>
{
    auto ptr = std::shared_ptr<Transport>(new Transport(std::move(comm_)));
    ptr->comm->set_transport(ptr);
    return ptr;
}

auto Transport::stop() const -> void
{
    comm->stop();
}

//////////////////////////////////////////////////
///// Topics
/////////////////////////////////////////////////
auto Transport::start_fullgraph_request(Graph *graph) -> std::pair<bool, bool>
{
    return comm->start_fullgraph_request(graph);
}

auto Transport::start_fullgraph_server(Graph *graph) -> void
{
    comm->start_fullgraph_server(graph);
}

auto Transport::start_topics_subcription(Graph *graph, bool show) -> void
{
    comm->start_subs(graph, show);
}

auto Transport::start_topics_publishing(Graph *graph, bool show) -> void
{
    comm->start_pubs(graph, show);
}

//////////////////////////////////////////////////
///// Agents info
/////////////////////////////////////////////////
auto Transport::get_connected_agents() const -> std::vector<std::string>
{
    std::unique_lock<std::mutex> lck(participant_set_mutex);
    std::vector<std::string> ret_vec;
    ret_vec.reserve(participant_set.size());
    for (auto &[k, _] : participant_set)
    {
        ret_vec.emplace_back(k);
    }
    return ret_vec;
}

//////////////////////////////////////////////////
///// write
/////////////////////////////////////////////////

auto Transport::write_node(mvreg<DSR::CRDTNode>&& node, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool
{
    return comm->write_node(std::forward<mvreg<DSR::CRDTNode>>(node), node_id, timestamp, agent_id);
}

auto Transport::write_edge(mvreg<DSR::CRDTEdge>&& edge, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool
{
    return comm->write_edge(std::forward<mvreg<DSR::CRDTEdge>>(edge), from, to, edge_type, timestamp, agent_id);
}

auto Transport::write_node_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t node_id, uint64_t timestamp, uint32_t agent_id) -> bool
{
    return comm->write_node_attributes(std::forward<std::vector<DSR::CRDTAttribute>>(attributes), node_id, timestamp, agent_id);
}

auto Transport::write_edge_attributes(std::vector<DSR::CRDTAttribute> &&attributes, uint64_t from, uint64_t to, std::string edge_type, uint64_t timestamp, uint32_t agent_id) -> bool
{
    return comm->write_edge_attributes(std::forward<std::vector<DSR::CRDTAttribute>>(attributes), from, to, edge_type, timestamp, agent_id);
}

auto Transport::write_graph(std::map<uint64_t, DSR::CRDTNode> &&graph, std::vector<uint64_t> deleted_nodes) -> bool
{
    return comm->write_graph(std::forward<std::map<uint64_t, DSR::CRDTNode>>(graph), std::forward<std::vector<uint64_t>>(deleted_nodes));
}

auto Transport::write_request(std::string& agent_name, uint32_t agent_id) -> bool
{
    return comm->write_request(agent_name, agent_id);
}
